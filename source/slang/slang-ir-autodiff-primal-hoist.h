// slang-ir-autodiff-primal-hoist.h
#pragma once

#include "slang-ir.h"
#include "slang-ir-insts.h"
#include "slang-ir-autodiff.h"
#include "slang-ir-autodiff-region.h"
#include "slang-ir-dominators.h"


namespace Slang
{
    struct IROutOfOrderCloneContext : public RefObject
    {
        IRCloneEnv cloneEnv;
        HashSet<IRUse*> pendingUses;

        void addOrReplacePendingUse(IRBuilder* builder, IRUse* use)
        {
            // Check if we have a clone for this use's operand.
            // If we do then simply replace the use with the clone.
            // Otherwise, add the use to the pending use set.
            auto operand = use->get();
            if (cloneEnv.mapOldValToNew.ContainsKey(operand))
            {
                auto clonedOperand = cloneEnv.mapOldValToNew[operand];
                builder->replaceOperand(use, clonedOperand);
            }
            else
            {
                pendingUses.Add(use);
            }
        }

        void addMapping(IRBuilder* builder, IRInst* inst, IRInst* clonedInst)
        {
            cloneEnv.mapOldValToNew.Add(inst, clonedInst);

            // Resolve pending uses.
            for (auto use = inst->firstUse; use;)
            {
                auto nextUse = use->nextUse;
                
                if (pendingUses.Contains(use))
                {
                    pendingUses.Remove(use);
                    builder->replaceOperand(use, clonedInst);
                }
                use = nextUse;
            }
        }

        IRInst* cloneInstOutOfOrder(IRBuilder* builder, IRInst* inst)
        {
            IRInst* clonedInst = cloneInst(&cloneEnv, builder, inst);

            UInt operandCount = clonedInst->getOperandCount();
            for (UInt ii = 0; ii < operandCount; ++ii)
            {
                auto oldOperand = inst->getOperand(ii);
                auto newOperand = clonedInst->getOperand(ii);

                if (oldOperand == newOperand)
                    pendingUses.Add(&clonedInst->getOperands()[ii]);
            }

            for (auto use = inst->firstUse; use;)
            {
                auto nextUse = use->nextUse;
                
                if (pendingUses.Contains(use))
                {
                    pendingUses.Remove(use);
                    builder->replaceOperand(use, clonedInst);
                }
                
                use = nextUse;
            }

            return clonedInst;
        }
    };

    struct InversionInfo
    {
        IRInst* instToInvert;
        List<IRInst*> requiredOperands;
        List<IRInst*> targetInsts;

        InversionInfo(
            IRInst* instToInvert,
            List<IRInst*> requiredOperands,
            List<IRInst*> targetInsts) :
            instToInvert(instToInvert),
            requiredOperands(requiredOperands),
            targetInsts(targetInsts)
        { }

        InversionInfo() : instToInvert(nullptr)
        { }

        InversionInfo applyMap(IRCloneEnv* env)
        {
            InversionInfo newInfo;
            if (env->mapOldValToNew.ContainsKey(instToInvert))
                newInfo.instToInvert = env->mapOldValToNew[instToInvert];
            
            for (auto inst : requiredOperands)
                if (env->mapOldValToNew.ContainsKey(inst))
                    newInfo.requiredOperands.add(env->mapOldValToNew[inst]);
                
            for (auto inst : targetInsts)
                if (env->mapOldValToNew.ContainsKey(inst))
                    newInfo.targetInsts.add(env->mapOldValToNew[inst]);
            
            return newInfo;
        }
    };

    struct HoistedPrimalsInfo : public RefObject
    {
        HashSet<IRInst*> storeSet;
        HashSet<IRInst*> recomputeSet;
        HashSet<IRInst*> invertSet;

        HashSet<IRInst*> instsToInvert;

        Dictionary<IRInst*, InversionInfo> invertInfoMap;

        RefPtr<HoistedPrimalsInfo> applyMap(IRCloneEnv* env)
        {
            RefPtr<HoistedPrimalsInfo> newPrimalsInfo = new HoistedPrimalsInfo();
            
            for (auto inst : this->storeSet)
                if (env->mapOldValToNew.ContainsKey(inst))
                    newPrimalsInfo->storeSet.Add(env->mapOldValToNew[inst]);
            
            for (auto inst : this->recomputeSet)
                if (env->mapOldValToNew.ContainsKey(inst))
                    newPrimalsInfo->recomputeSet.Add(env->mapOldValToNew[inst]);
                
            for (auto inst : this->invertSet)
                if (env->mapOldValToNew.ContainsKey(inst))
                    newPrimalsInfo->invertSet.Add(env->mapOldValToNew[inst]);
            
            for (auto inst : this->instsToInvert)
                if (env->mapOldValToNew.ContainsKey(inst))
                    newPrimalsInfo->instsToInvert.Add(env->mapOldValToNew[inst]);

            for (auto kvpair : this->invertInfoMap)
                if (env->mapOldValToNew.ContainsKey(kvpair.Key))
                    newPrimalsInfo->invertInfoMap[env->mapOldValToNew[kvpair.Key]] = kvpair.Value.applyMap(env);
            
            return newPrimalsInfo;
        }

        void merge(HoistedPrimalsInfo* info)
        {
            for (auto inst : info->storeSet)
                storeSet.Add(inst);

            for (auto inst : info->recomputeSet)
                recomputeSet.Add(inst);

            for (auto inst : info->invertSet)
                invertSet.Add(inst);

            for (auto inst : info->instsToInvert)
                instsToInvert.Add(inst);

            for (auto kvpair : info->invertInfoMap)
                invertInfoMap[kvpair.Key] = kvpair.Value;
        }
    };

    struct HoistResult
    {
        enum Mode
        {
            Store,
            Recompute,
            Invert,

            None
        };

        Mode mode;
        
        IRInst* instToStore = nullptr;
        IRInst* instToRecompute = nullptr;
        InversionInfo inversionInfo;

        HoistResult(Mode mode, IRInst* target) :
            mode(mode)
        { 
            switch (mode)
            {
            case Mode::Store:
                instToStore = target;
                break;
            case Mode::Recompute:
                instToRecompute = target;
                break;
            case Mode::Invert:
                SLANG_UNEXPECTED("Wrong constructor for HoistResult::Mode::Invert");
                break;
            default:
                SLANG_UNEXPECTED("Unhandled hoist mode");
                break;
            }
        }

        HoistResult(InversionInfo info) : 
            mode(Mode::Invert), inversionInfo(info)
        { }

        static HoistResult store(IRInst* inst)
        {
            return HoistResult(Mode::Store, inst);
        }

        static HoistResult recompute(IRInst* inst)
        {
            return HoistResult(Mode::Recompute, inst);
        }

        static HoistResult invert(InversionInfo inst)
        {
            return HoistResult(inst);
        }
    };

    
    // Information on which insts are to be stored, recomputed
    // and inverted within a single function.
    // This data structure also holds a map of raw HoistResult
    // objects to provide more information to later passes.
    // 
    struct CheckpointSetInfo : public RefObject
    {
        HashSet<IRInst*> storeSet;
        HashSet<IRInst*> recomputeSet;
        HashSet<IRInst*> invertSet;

        Dictionary<IRInst*, InversionInfo> invInfoMap;
    };

    struct BlockSplitInfo : public RefObject
    {
        // Maps primal to differential blocks from the unzip step.
        Dictionary<IRBlock*, IRBlock*> diffBlockMap;
    };

    class AutodiffCheckpointPolicyBase : public RefObject
    {
    public:

        AutodiffCheckpointPolicyBase(IRModule* module) : module(module)
        { }

        RefPtr<HoistedPrimalsInfo> processFunc(IRGlobalValueWithCode* func, BlockSplitInfo* info);

        void addOverride(IRUse* use, HoistResult result)
        {
            overrideMap[use] = result;
        }

        HoistResult classify(IRUse* use)
        {
            if (overrideMap.ContainsKey(use))
                return overrideMap[use];
            return _classify(use);
        }

        // Do pre-processing on the function (mainly for 
        // 'global' checkpointing methods that consider the entire
        // function)
        // 
        virtual void preparePolicy(IRGlobalValueWithCode* func) = 0;

        virtual HoistResult _classify(IRUse* diffBlockUse) = 0;

     protected:

        IRModule*                            module;
        Dictionary<IRUse*, HoistResult> overrideMap;
    };

    class DefaultCheckpointPolicy : public AutodiffCheckpointPolicyBase
    {
    public:

        DefaultCheckpointPolicy(IRModule* module)
            : AutodiffCheckpointPolicyBase(module)
        { }

        virtual void preparePolicy(IRGlobalValueWithCode* func);
        virtual HoistResult _classify(IRUse* use);

        RefPtr<IRDominatorTree> domTree;
    };

    RefPtr<HoistedPrimalsInfo> applyCheckpointSet(
        CheckpointSetInfo* checkpointInfo,
        IRGlobalValueWithCode* func,
        BlockSplitInfo* splitInfo,
        HashSet<IRUse*> pendingUses);

    RefPtr<HoistedPrimalsInfo> ensurePrimalAvailability(
        HoistedPrimalsInfo* hoistInfo,
        IRGlobalValueWithCode* func,
        Dictionary<IRBlock*, List<IndexTrackingInfo*>> indexedBlockInfo);

};
