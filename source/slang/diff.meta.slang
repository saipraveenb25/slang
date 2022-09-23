
/// Modifer to mark a function for forward-mode differentiation.
/// i.e. the compiler will automatically generate a new function
/// that computes the jacobian-vector product of the original.
syntax __differentiate_jvp : JVPDerivativeModifier;

// Custom JVP Function reference
__attributeTarget(FuncDecl)
attribute_syntax [__custom_jvp(function)]   : CustomJVPAttribute;


    /// Pair type that serves to wrap the primal and
    /// differential types of an arbitrary type T.
__generic<T : IDifferentiable>
__magic_type(DifferentialPairType)
__intrinsic_type($(kIROp_DifferentialPairType))
struct __DifferentialPair
{

    __intrinsic_op($(kIROp_MakeDifferentialPair))
    __init(T _primal, T.Differential _differential);

    __intrinsic_op($(kIROp_DifferentialPairGetDifferential))
    T.Differential d();

    T.Differential getDifferential()
    {
        return d();
    }

    __intrinsic_op($(kIROp_DifferentialPairGetPrimal))
    T p();

    T getPrimal()
    {
        return p();
    }
};
