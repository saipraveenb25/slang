#include "slang-ir-peephole.h"
#include "slang-ir-inst-pass-base.h"

namespace Slang
{
struct PeepholeContext : InstPassBase
{
    PeepholeContext(IRModule* inModule)
        : InstPassBase(inModule)
    {}

    bool changed = false;

    bool tryFoldElementExtractFromUpdateInst(IRInst* inst)
    {
        bool isAccessChainEqual = false;
        bool isAccessChainNotEqual = false;
        List<IRInst*> chainKey;
        IRInst* chainNode = inst;
        for (;;)
        {
            switch (chainNode->getOp())
            {
            case kIROp_FieldExtract:
            case kIROp_GetElement:
                chainKey.add(chainNode->getOperand(1));
                chainNode = chainNode->getOperand(0);
                continue;
            }
            break;
        }
        chainKey.reverse();
        if (auto updateInst = as<IRUpdateElement>(chainNode))
        {
            if (updateInst->getAccessKeyCount() > (UInt)chainKey.getCount())
                return false;

            isAccessChainEqual = true;
            for (UInt i = 0; i < (UInt)chainKey.getCount(); i++)
            {
                if (updateInst->getAccessKey(i) != chainKey[i])
                {
                    isAccessChainEqual = false;
                    if (as<IRStructKey>(chainKey[i]))
                    {
                        isAccessChainNotEqual = true;
                        break;
                    }
                    else
                    {
                        if (auto constIndex1 = as<IRIntLit>(updateInst->getAccessKey(i)))
                        {
                            if (auto constIndex2 = as<IRIntLit>(chainKey[i]))
                            {
                                if (constIndex1->getValue() != constIndex2->getValue())
                                {
                                    isAccessChainNotEqual = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            if (isAccessChainEqual)
            {
                auto remainingKeys = chainKey.getArrayView(
                    updateInst->getAccessKeyCount(),
                    chainKey.getCount() - updateInst->getAccessKeyCount());
                if (remainingKeys.getCount() == 0)
                {
                    inst->replaceUsesWith(updateInst->getElementValue());
                    inst->removeAndDeallocate();
                    return true;
                }
                else if (remainingKeys.getCount() > 0)
                {
                    IRBuilder builder(&sharedBuilderStorage);
                    builder.setInsertBefore(inst);
                    auto newValue = builder.emitElementExtract(updateInst->getElementValue(), remainingKeys);
                    inst->replaceUsesWith(newValue);
                    inst->removeAndDeallocate();
                    return true;
                }
            }
            else if (isAccessChainNotEqual)
            {
                IRBuilder builder(&sharedBuilderStorage);
                builder.setInsertBefore(inst);
                auto newInst = builder.emitElementExtract(updateInst->getOldValue(), chainKey.getArrayView());
                inst->replaceUsesWith(newInst);
                inst->removeAndDeallocate();
                return true;
            }
        }
        return false;
    }

    void processInst(IRInst* inst)
    {
        switch (inst->getOp())
        {
        case kIROp_GetResultError:
            if (inst->getOperand(0)->getOp() == kIROp_MakeResultError)
            {
                inst->replaceUsesWith(inst->getOperand(0)->getOperand(0));
                changed = true;
            }
            break;
        case kIROp_GetResultValue:
            if (inst->getOperand(0)->getOp() == kIROp_MakeResultValue)
            {
                inst->replaceUsesWith(inst->getOperand(0)->getOperand(0));
                inst->removeAndDeallocate();
                changed = true;
            }
            break;
        case kIROp_IsResultError:
            if (inst->getOperand(0)->getOp() == kIROp_MakeResultError)
            {
                IRBuilder builder(&sharedBuilderStorage);
                inst->replaceUsesWith(builder.getBoolValue(true));
                inst->removeAndDeallocate();
                changed = true;
            }
            else if (inst->getOperand(0)->getOp() == kIROp_MakeResultValue)
            {
                IRBuilder builder(&sharedBuilderStorage);
                inst->replaceUsesWith(builder.getBoolValue(false));
                inst->removeAndDeallocate();
                changed = true;
            }
            break;
        case kIROp_GetTupleElement:
            if (inst->getOperand(0)->getOp() == kIROp_MakeTuple)
            {
                auto element = inst->getOperand(1);
                if (auto intLit = as<IRIntLit>(element))
                {
                    inst->replaceUsesWith(inst->getOperand(0)->getOperand((UInt)intLit->value.intVal));
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            break;
        case kIROp_FieldExtract:
            if (inst->getOperand(0)->getOp() == kIROp_MakeStruct)
            {
                auto field = as<IRFieldExtract>(inst)->field.get();
                Index fieldIndex = -1;
                auto structType = as<IRStructType>(inst->getOperand(0)->getDataType());
                if (structType)
                {
                    Index i = 0;
                    for (auto sfield : structType->getFields())
                    {
                        if (sfield->getKey() == field)
                        {
                            fieldIndex = i;
                            break;
                        }
                        i++;
                    }
                    if (fieldIndex != -1 && fieldIndex < (Index)inst->getOperand(0)->getOperandCount())
                    {
                        inst->replaceUsesWith(inst->getOperand(0)->getOperand((UInt)fieldIndex));
                        inst->removeAndDeallocate();
                        changed = true;
                    }
                }
            }
            else
            {
                changed = tryFoldElementExtractFromUpdateInst(inst);
            }
            break;
        case kIROp_GetElement:
            if (inst->getOperand(0)->getOp() == kIROp_MakeArray)
            {
                auto index = as<IRIntLit>(as<IRGetElement>(inst)->getIndex());
                if (!index)
                    break;
                auto opCount = inst->getOperand(0)->getOperandCount();
                if ((UInt)index->getValue() < opCount)
                {
                    inst->replaceUsesWith(inst->getOperand(0)->getOperand((UInt)index->getValue()));
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            else if (inst->getOperand(0)->getOp() == kIROp_MakeArrayFromElement)
            {
                inst->replaceUsesWith(inst->getOperand(0)->getOperand(0));
                inst->removeAndDeallocate();
                changed = true;
            }
            else
            {
                changed = tryFoldElementExtractFromUpdateInst(inst);
            }
            break;
        case kIROp_UpdateElement:
            {
                auto updateInst = as<IRUpdateElement>(inst);
                if (updateInst->getAccessKeyCount() != 1)
                    break;
                auto key = updateInst->getAccessKey(0);
                if (auto constIndex = as<IRIntLit>(key))
                {
                    auto oldVal = inst->getOperand(0);
                    if (oldVal->getOp() == kIROp_MakeArray ||
                        oldVal->getOp() == kIROp_MakeArrayFromElement)
                    {
                        auto arrayType = as<IRArrayType>(inst->getDataType());
                        if (!arrayType) break;
                        auto arraySize = as<IRIntLit>(arrayType->getElementCount());
                        if (!arraySize) break;
                        List<IRInst*> args;
                        for (IRIntegerValue i = 0; i < arraySize->getValue(); i++)
                        {
                            IRInst* arg = nullptr;
                            if (i < (IRIntegerValue)oldVal->getOperandCount())
                                arg = oldVal->getOperand((UInt)i);
                            else if (oldVal->getOperandCount() != 0)
                                arg = oldVal->getOperand(0);
                            else
                                break;
                            if (i == (IRIntegerValue)constIndex->getValue())
                                arg = inst->getOperand(2);
                            args.add(arg);
                        }
                        if (args.getCount() == arraySize->getValue())
                        {
                            IRBuilder builder(&sharedBuilderStorage);
                            builder.setInsertBefore(inst);
                            auto makeArray = builder.emitMakeArray(arrayType, (UInt)args.getCount(), args.getBuffer());
                            inst->replaceUsesWith(makeArray);
                            inst->removeAndDeallocate();
                            changed = true;
                        }
                    }
                }
                else if (auto structKey = as<IRStructKey>(key))
                {
                    auto oldVal = inst->getOperand(0);
                    if (oldVal->getOp() == kIROp_MakeStruct)
                    {
                        auto structType = as<IRStructType>(inst->getDataType());
                        if (!structType) break;
                        List<IRInst*> args;
                        UInt i = 0;
                        bool isValid = true;
                        for (auto field : structType->getFields())
                        {
                            IRInst* arg = nullptr;
                            if (i < oldVal->getOperandCount())
                                arg = oldVal->getOperand(i);
                            if (field->getKey() == inst->getOperand(1))
                                arg = inst->getOperand(2);
                            if (arg)
                            {
                                args.add(arg);
                            }
                            else
                            {
                                isValid = false;
                                break;
                            }
                            i++;
                        }
                        if (isValid)
                        {
                            IRBuilder builder(&sharedBuilderStorage);
                            builder.setInsertBefore(inst);
                            auto makeStruct = builder.emitMakeStruct(structType, (UInt)args.getCount(), args.getBuffer());
                            inst->replaceUsesWith(makeStruct);
                            inst->removeAndDeallocate();
                            changed = true;
                        }
                    }
                }
            }
            break;
        case kIROp_CastPtrToBool:
            {
                auto ptr = inst->getOperand(0);
                IRBuilder builder(&sharedBuilderStorage);
                builder.setInsertBefore(inst);
                auto neq = builder.emitNeq(ptr, builder.getNullVoidPtrValue());
                inst->replaceUsesWith(neq);
                inst->removeAndDeallocate();
                changed = true;
            }
            break;
        case kIROp_IsType:
            {
                auto isTypeInst = as<IRIsType>(inst);
                auto actualType = isTypeInst->getValue()->getDataType();
                if (isTypeEqual(actualType, (IRType*)isTypeInst->getTypeOperand()))
                {
                    IRBuilder builder(&sharedBuilderStorage);
                    builder.setInsertBefore(inst);
                    auto trueVal = builder.getBoolValue(true);
                    inst->replaceUsesWith(trueVal);
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            break;
        case kIROp_Reinterpret:
        case kIROp_BitCast:
        case kIROp_IntCast:
        case kIROp_FloatCast:
            {
                if (isTypeEqual(inst->getOperand(0)->getDataType(), inst->getDataType()))
                {
                    inst->replaceUsesWith(inst->getOperand(0));
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            break;
        case kIROp_UnpackAnyValue:
            {
                if (inst->getOperand(0)->getOp() == kIROp_PackAnyValue)
                {
                    if (isTypeEqual(inst->getOperand(0)->getOperand(0)->getDataType(), inst->getDataType()))
                    {
                        inst->replaceUsesWith(inst->getOperand(0)->getOperand(0));
                        inst->removeAndDeallocate();
                        changed = true;
                    }
                }
            }
            break;
        case kIROp_PackAnyValue:
        {
            // Pack(obj: anyValueN) : anyValueN --> obj
            if (isTypeEqual(inst->getOperand(0)->getDataType(), inst->getDataType()))
            {
                inst->replaceUsesWith(inst->getOperand(0));
                inst->removeAndDeallocate();
                changed = true;
            }
        }
        break;
        case kIROp_GetOptionalValue:
            {
                if (inst->getOperand(0)->getOp() == kIROp_MakeOptionalValue)
                {
                    inst->replaceUsesWith(inst->getOperand(0)->getOperand(0));
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            break;
        case kIROp_OptionalHasValue:
            {
                if (inst->getOperand(0)->getOp() == kIROp_MakeOptionalValue)
                {
                    IRBuilder builder(&sharedBuilderStorage);
                    builder.setInsertBefore(inst);
                    auto trueVal = builder.getBoolValue(true);
                    inst->replaceUsesWith(trueVal);
                    inst->removeAndDeallocate();
                    changed = true;
                }
                else if (inst->getOperand(0)->getOp() == kIROp_MakeOptionalNone)
                {
                    IRBuilder builder(&sharedBuilderStorage);
                    builder.setInsertBefore(inst);
                    auto falseVal = builder.getBoolValue(false);
                    inst->replaceUsesWith(falseVal);
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            break;
        case kIROp_GetNativePtr:
            {
                if (inst->getOperand(0)->getOp() == kIROp_PtrLit)
                {
                    inst->replaceUsesWith(inst->getOperand(0));
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            break;
        case kIROp_MakeExistential:
            {
                if (inst->getOperand(0)->getOp() == kIROp_ExtractExistentialValue)
                {
                    inst->replaceUsesWith(inst->getOperand(0)->getOperand(0));
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            break;
        case kIROp_LookupWitness:
            {
                if (inst->getOperand(0)->getOp() == kIROp_WitnessTable)
                {
                    auto wt = as<IRWitnessTable>(inst->getOperand(0));
                    auto key = inst->getOperand(1);
                    for (auto item : wt->getChildren())
                    {
                        if (auto entry = as<IRWitnessTableEntry>(item))
                        {
                            if (entry->getRequirementKey() == key)
                            {
                                auto value = entry->getSatisfyingVal();
                                inst->replaceUsesWith(value);
                                inst->removeAndDeallocate();
                                changed = true;
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case kIROp_DefaultConstruct:
            {
                IRBuilder builder(&sharedBuilderStorage);
                builder.setInsertBefore(inst);
                // See if we can replace the default construct inst with concrete values.
                if (auto newCtor = builder.emitDefaultConstruct(inst->getFullType(), false))
                {
                    inst->replaceUsesWith(newCtor);
                    inst->removeAndDeallocate();
                    changed = true;
                }
            }
            break;
        default:
            break;
        }
    }

    bool processFunc(IRInst* func)
    {
        SharedIRBuilder* sharedBuilder = &sharedBuilderStorage;
        sharedBuilder->init(module);
        sharedBuilderStorage.deduplicateAndRebuildGlobalNumberingMap();
        bool result = false;
        for (;;)
        {
            changed = false;
            processChildInsts(func, [this](IRInst* inst) { processInst(inst); });
            if (changed)
                result = true;
            else
                break;
        }
        return result;
    }

    bool processModule()
    {
        return processFunc(module->getModuleInst());
    }
};

bool peepholeOptimize(IRModule* module)
{
    PeepholeContext context = PeepholeContext(module);
    return context.processModule();
}

bool peepholeOptimize(IRInst* func)
{
    PeepholeContext context = PeepholeContext(func->getModule());
    return context.processFunc(func);
}

} // namespace Slang
