//TEST:SIMPLE(filecheck=CHECK): -target cuda

__require_prelude(cuda, "#include \"cuda_gemm_prelude.cuh\"")
void __gemm(Ptr<float> a, Ptr<float> b, Ptr<float> c, Ptr<float> d, int m, int n, int k)
{
    __target_switch
    {
    case cuda:
        __intrinsic_asm "tf32gemm($0, $1, $2, $3, $4, $5, $6)";
    }
}

[AutoPyBindCUDA]
[CUDAKernel]
void gemm(TensorView<float> a, TensorView<float> b, TensorView<float> c, TensorView<float> d)
{
    __gemm(a.data_ptr(), b.data_ptr(), c.data_ptr(), d.data_ptr(), a.size(0), a.size(1), b.size(1));
}