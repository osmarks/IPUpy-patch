
#include <poplar/Vertex.hpp> 


class ComplexOp: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<float>> X;

    bool compute() {
        
        #pragma IPUpy_start [stdout=printBuf, X=X<float>]
        
        print('Beginning ComplexOp...')

        class Worker:
            def __init__(self, X):
                self.X = X
            
            def run(self, *args):
                return self.run_impl(X, *args)

        def run_impl(X, transform, condition):
            return list(
                    filter(
                        condition,
                        [transform(x) for x in X]
                    )
                )

        w = Worker(X)
        w.run_impl = run_impl
        result = w.run(
            lambda x: -x,
            lambda x: x < -5 or x > 3
        )

        print('Result: {}'.format(result))

        #pragma IPUpy_end
        return true;
    }
};


