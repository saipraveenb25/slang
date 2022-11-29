// slang-ir-autodiff-unzip.h
#pragma once

#include "slang-ir.h"
#include "slang-ir-insts.h"
#include "slang-compiler.h"

#include "slang-ir-autodiff.h"
#include "slang-autodiff-propagate.h"

namespace Slang
{

struct DiffUnzipPass
{
    AutoDiffSharedContext*                  autodiffContext;

    IRCloneEnv                              cloneEnv;

    DiffUnzipPass(AutoDiffSharedContext* autodiffContext) : 
        autodiffContext(autodiffContext)
    { }

    void unzipDiffInsts(IRFunc* func, IRFunc* unzippedFunc)
    {
        IRBuilder builderStorage;
        builderStorage.init(autodiffContext->sharedBuilder);
        
        IRBuilder* builder = &builderStorage;

        builder->setInsertInto(unzippedFunc);

        // Work with two-block functions for now.
        SLANG_ASSERT(func->getFirstBlock() != nullptr);
        SLANG_ASSERT(func->getFirstBlock()->getNextBlock() != nullptr);
        SLANG_ASSERT(func->getFirstBlock()->getNextBlock()->getNextBlock() == nullptr);

        // Ignore the first block (this is reserved for parameters), start
        // at the second block. (For now, we work with only a single block of insts)
        // TODO: expand to handle multi-block functions later.

        IRBlock* mainBlock = func->getFirstBlock()->getNextBlock();
        
        IRBlock* primalBlock = builder->emitBlock();
        IRBlock* diffBlock = builder->emitBlock(); 

        // Mark the differential block as a differential inst.
        builder->markInstAsDifferential(diffBlock);

        // Split this block into two. This method should also emit
        // a branch statement from primalBlock to diffBlock.
        // 
        splitBlock(mainBlock, primalBlock, diffBlock);

        // Clone in the parameter block.
        // TODO: Will need to generalize this so we can handle multi-block
        // reverse-mode.
        cloneEnv.mapOldValToNew[mainBlock] = primalBlock;
        IRBlock* paramBlock = func->getFirstBlock();
        IRInst* newParamBlock = cloneInst(&cloneEnv, builder, paramBlock);

        newParamBlock->insertBefore(primalBlock);
    }

    void splitBlock(IRBlock* mainBlock, IRBlock* primalBlock, IRBlock* diffBlock)
    {
        // Make two builders for primal and differential blocks.
        IRBuilder primalBuilder;
        primalBuilder.init(autodiffContext->sharedBuilder);
        primalBuilder.setInsertInto(primalBlock);

        IRBuilder diffBuilder;
        diffBuilder.init(autodiffContext->sharedBuilder);
        diffBuilder.setInsertInto(diffBlock);

        for (auto child = mainBlock->getFirstChild(); child;)
        {
            IRInst* nextChild = child->getNextInst();

            if (isDifferentialInst(child) || as<IRTerminatorInst>(child))
            {
                auto newInst = cloneInst(&cloneEnv, &diffBuilder, child);
                child->replaceUsesWith(newInst);
                child->removeAndDeallocate();
            }
            else
            {
                auto newInst = cloneInst(&cloneEnv, &primalBuilder, child);
                child->replaceUsesWith(newInst);
                child->removeAndDeallocate();
            }

            child = nextChild;
        }

        // Nothing should be left in the original block.
        SLANG_ASSERT(mainBlock->getFirstChild() == nullptr);

        // Branch from primal to differential block.
        // Functionally, the new blocks should produce the same output as the
        // old block.
        primalBuilder.emitBranch(diffBlock);
    }
};

}
