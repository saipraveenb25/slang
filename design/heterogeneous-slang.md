
## Option 1: Slang -> C++ w/ Embedded Slang compiler
- Host code compiled to C++ 
- Device code embedded as data in the C++ code
- Slang compilation API exposed to the host-code, which compiles on the fly.
    - Optionally, pre-compile device code to SPIR-V and embed.
- Fully explicit control in Slang
- Similar to CUDA model.

## Option 2: High-level VM Model 

- High-level Virtual Machine
- Byte-code is SSA-style
    - Infinite Registers
    - Structured control flow
    - No memory model (e.g., no stack, heap, etc.)
    - Supports NDArrays/Tensors as first-class citizens
    - Supports Slang types as first-class citizens for JIT specialization
- Runners take care of memory allocation, data movement, etc.
    - Scheduling decisions taken care of by the runner
    - Ahead-of-time scanning to determine memory requirements
    - Can be optionally taken care of by tooling
        - e.g. Default scheduler that translates high-level bytecode to low-level bytecode
- Can optionally be lowered to more concrete byte-code for faster execution.
- Requires a new target GRAPH_VM

- Plugs into rendering engines more easily: they come with their own schedulers and memory management systems.
- Retains enough high-level information for graph targets (e.g. TensorRT) to do their own target-specific optimizations.

- Example:
```
Section("META") # Metadata about the program
%m0 : OpDecorate NameHint %e0 "main"

Section("TYPES") # Type layout info
%t0 : typetype = tensor 4 f32

Section("FUNCS") # VM executable code
%e0 = OpFunc
%e1 = OpBlock
%e2 : datasource = OpFileDataSource "input.bin" Binary
%e3 : %t0 = OpDeserialize %e2
%e4 : %t0 = OpTensorEmpty %e3
%e4 : %t0 = OpDispatch Compute %d0 %e3
%e5 : %t0 = OpDispatch Compute %d1 %e4
OpReturn %e5

Section("DEVICE") # Serialized device code
%d0 = slang "tonemap" {
    // Binary IR and AST module for tonemap compute shader
    void tonemap(Tensor<float, 4> input, WTensor<float, 4> output)
    {
        threadId = getThreadId();
        output[threadId] = input[threadId] / (input[threadId] + 1);
    }
}

%d1 = slang "scale" {
    // Binary IR and AST module for scale compute shader
}
```

## Option 3: Low-level VM Model

- Low-level Virtual Machine
- Byte-code is close to assembly, but still a virtual instruction set
    - Working Set of Registers
    - Jump-based control flow (functions are flat sequences of instructions)
    - Most special types will need to be lowered to simple memory operations.
- "Dumb" Runners
    - Just execute the byte-code; very few optimizations/scheduling decisions.
- Simple version already present in Slang (STAGE_DISPATCH)
- Example:
```
func main: ws=16 cs=192 pc=0 ps=0 rs=0
  0:    call.8              ws:0, GenericImpl.eval, ws:0, f32(1)
  50:   copy.4              ws:8, ws:0
  80:   print               str:"result is %f\n", ws:0
  B0:   ret                 
func GenericImpl.eval: ws=16 cs=128 pc=2 ps=4 rs=8
  0:    copy.4              ws:4, ws:0
  30:   copy.4              ws:8, ws:0
  60:   ret.8               ws:4
```
- Less flexible, fewer optimizations on the byte-code itself.

## Option 4: Multi-Level Model?
 - ?



- Add `context`
- ContextDecl. Add parser/checking stuff like that.
- 
context myCtx
{
    // contextual values, types and functions.
};

void foo[myCtx](int x)
{

}

Reference
{
    target=foo
    
}

Reference
{
    target=foo,
    args= 
    [
        CPU, f32
    ],
    mapping= 
    {
        myCtx -> CPUCtx;
        T -> f32;
    }
}