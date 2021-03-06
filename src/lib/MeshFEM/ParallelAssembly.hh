#ifndef PARALLELASSEMBLY_HH
#define PARALLELASSEMBLY_HH

#include "SparseMatrices.hh"
#include "Eigen/Dense"
template<typename Real_> using VecX_T = Eigen::Matrix<Real_, -1, 1>;

#if MESHFEM_WITH_TBB
#include <tbb/tbb.h>
#include <tbb/parallel_for.h>
#include <tbb/enumerable_thread_specific.h>

// Energy summation

template<typename F, typename Real_>
struct SummationComputer {
    SummationComputer(F& f, const size_t nvars, VecX_T<Real_>& summands) : m_f(f), m_nvars(nvars), m_summands(summands) { }

    void operator()(const tbb::blocked_range<size_t>& r) const {
        for (size_t si = r.begin(); si < r.end(); ++si) { m_f(si, m_summands); }
    }
private:
    F& m_f;
    size_t m_nvars;
    VecX_T<Real_>& m_summands;
};

template<typename F, typename Real_>
SummationComputer<F, Real_> make_summation_computer(F& f, size_t nvars, VecX_T<Real_>& summands) {
    return SummationComputer<F, Real_>(f, nvars, summands);
}

template<typename Real_, typename PerElemSummand>
Real_ summation_parallel(const PerElemSummand& summand, const size_t numElems, bool dontSetZero = false) {
    VecX_T<Real_> summands(numElems);
    if (!dontSetZero) summands.setZero();
    tbb::parallel_for(tbb::blocked_range<size_t>(0, numElems),
        make_summation_computer(summand, numElems, summands));

    return summands.sum();
}

// Dense vector/matrix assembly (e.g., for gradient)

template<class DenseMatrixType>
struct DenseAssemblerData {
    DenseMatrixType A;
    bool constructed = false;
};

template<class DenseMatrixType>
using DALocalData = tbb::enumerable_thread_specific<DenseAssemblerData<DenseMatrixType>>;

template<typename F, class DenseMatrixType>
struct DenseAssembler {
    DenseAssembler(F &f, const size_t nrows, const size_t ncols, DALocalData<DenseMatrixType>& locals)
        : m_f(f), m_nrows(nrows), m_ncols(ncols), m_locals(locals) { }

    void operator()(const tbb::blocked_range<size_t> &r) const {
        DenseAssemblerData<DenseMatrixType> &data = m_locals.local();
        if (!data.constructed) { data.A.setZero(m_nrows, m_ncols); data.constructed = true; }
        for (size_t si = r.begin(); si < r.end(); ++si) { m_f(si, data.A); }
    }
private:
    F &m_f;
    size_t m_nrows, m_ncols;
    DALocalData<DenseMatrixType> &m_locals;
};

template<typename F, class DenseMatrixType>
DenseAssembler<F, DenseMatrixType> make_dense_assembler(F &f, size_t nrows, size_t ncols, DALocalData<DenseMatrixType> &locals) {
    return DenseAssembler<F, DenseMatrixType>(f, nrows, ncols, locals);
}

template<typename PerElemAssembler, class Derived>
void assemble_parallel(const PerElemAssembler &assembler, Eigen::MatrixBase<Derived> &A, const size_t numElems) {
    using DenseMatrixType = Eigen::Matrix<typename Derived::Scalar, Derived::RowsAtCompileTime, Derived::ColsAtCompileTime>;
    DALocalData<DenseMatrixType> daLocalData;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, numElems),
                      make_dense_assembler(assembler, A.rows(), A.cols(), daLocalData));

    for (const auto &data : daLocalData)
        A += data.A;
}

// Hessian assembly

template<typename Real_>
struct HessianAssemblerData {
    CSCMatrix<SuiteSparse_long, Real_> H;
    bool constructed = false;
};

template<typename Real_>
using HALocalData = tbb::enumerable_thread_specific<HessianAssemblerData<Real_>>;

template<typename F, typename Real_>
struct HessianAssembler {
    using CSCMat = CSCMatrix<SuiteSparse_long, Real_>;
    HessianAssembler(F &f, const CSCMat &H, HALocalData<Real_> &locals) : Hsp(H), m_f(f), m_locals(locals) { }

    void operator()(const tbb::blocked_range<size_t> &r) const {
        HessianAssemblerData<Real_> &data = m_locals.local();
        if (!data.constructed) { data.H.zeros_like(Hsp); data.constructed = true; }
        for (size_t si = r.begin(); si < r.end(); ++si) { m_f(si, data.H); }
    }

    const CSCMat &Hsp; // sparsity pattern for H
private:
    F &m_f;
    HALocalData<Real_> &m_locals;
};

template<typename F, typename Real_>
HessianAssembler<F, Real_> make_hessian_assembler(F &f, const CSCMatrix<SuiteSparse_long, Real_> &H, HALocalData<Real_> &locals) {
    return HessianAssembler<F, Real_>(f, H, locals);
}

// Assemble a Hessian in parallel
template<typename PerElemAssembler, typename Real_>
void assemble_parallel(const PerElemAssembler &assembler, CSCMatrix<SuiteSparse_long, Real_> &H, const size_t numElems) {
    HALocalData<Real_> haLocalData;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, numElems),
                      make_hessian_assembler(assembler, H, haLocalData));

    for (const HessianAssemblerData<Real> &data : haLocalData)
        H.addWithIdenticalSparsity(data.H);
}

#else

// Fallback to serial assembly.
template<typename PerElemAssembler, typename Real_>
void assemble_parallel(const PerElemAssembler &assembler, CSCMatrix<SuiteSparse_long, Real_> &H, const size_t numElems) {
    for (size_t ei = 0; ei < numElems; ++ei)
        assembler(ei, H);
}
#endif

#endif /* end of include guard: PARALLELASSEMBLY_HH */
