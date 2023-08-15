#include "slang-spirv-val.h"

namespace Slang
{

SlangResult debugDisassembleSPIRV(const List<uint8_t>& spirv, String& outDis)
{
    CommandLine commandLine;
    commandLine.m_executableLocation.setName("spirv-dis");
    RefPtr<Process> p;
    const auto createResult = Process::create(commandLine, 0, p);
    // If we failed to even start the process, then validation isn't available
    SLANG_RETURN_ON_FAIL(createResult);
    const auto in = p->getStream(StdStreamType::In);
    const auto out = p->getStream(StdStreamType::Out);
    // Write the assembly
    SLANG_RETURN_ON_FAIL(in->write(spirv.getBuffer(), spirv.getCount()));
    in->close();
    // Wait for it to finish
    if (!p->waitForTermination(1000))
        return SLANG_FAIL;

    List<Byte> outData;
    SLANG_RETURN_ON_FAIL(StreamUtil::readAll(out, 0, outData));
    outDis = String((const char*)outData.getBuffer());
    return SLANG_OK;
}

SlangResult debugValidateSPIRV(const List<uint8_t>& spirv)
{
    // Set up our process
    CommandLine commandLine;
    commandLine.m_executableLocation.setName("spirv-val");
    RefPtr<Process> p;
    const auto createResult = Process::create(commandLine, 0, p);
    // If we failed to even start the process, then validation isn't available
    if(SLANG_FAILED(createResult))
        return SLANG_E_NOT_AVAILABLE;
    const auto in = p->getStream(StdStreamType::In);
    const auto out = p->getStream(StdStreamType::Out);
    const auto err = p->getStream(StdStreamType::ErrorOut);

    // Write the assembly
    SLANG_RETURN_ON_FAIL(in->write(spirv.getBuffer(), spirv.getCount()));
    in->close();

    // Wait for it to finish
    if(!p->waitForTermination(1000))
        return SLANG_FAIL;


    // TODO: allow inheriting stderr in Process
    List<Byte> outData;
    SLANG_RETURN_ON_FAIL(StreamUtil::readAll(out, 0, outData));
    fwrite(outData.getBuffer(), outData.getCount(), 1, stderr);
    outData.clear();
    SLANG_RETURN_ON_FAIL(StreamUtil::readAll(err, 0, outData));
    fwrite(outData.getBuffer(), outData.getCount(), 1, stderr);
    const auto ret = p->getReturnValue();
    if (SLANG_FAILED(ret))
    {
        String spirvDis;
        debugDisassembleSPIRV(spirv, spirvDis);
        fwrite(spirvDis.getBuffer(), spirvDis.getLength(), 1, stderr);
    }

    return ret == 0 ? SLANG_OK : SLANG_FAIL;
}

}
