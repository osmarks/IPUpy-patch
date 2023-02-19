
#include <poplar/Vertex.hpp> 


class pyvertex: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<char>> printBuf;
    poplar::InOut<poplar::Vector<int>> X;
    poplar::InOut<poplar::Vector<int>> Y;

    bool compute() {

        

        #pragma IPUpy_start [stdout=printBuf, X, Y]
        
        from uarray import array
        print('NEW\npayload is {payload}\nhere'.format(payload=array))
        print('__name__ is', __name__)
        
        Y[:] = array('i', 
            map(
                lambda x: x if x > 1 else -1,
                filter(
                    lambda x: x > 0, 
                    X
                )
            )
        )
        sum([1,2,3])

        #pragma IPUpy_end

        return true;
    }
};


