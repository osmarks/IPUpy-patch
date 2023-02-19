
#include <poplar/Vertex.hpp> 




class LogicalConv3Cpp: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<int>> input;
    poplar::Output<poplar::Vector<int>> output;

    bool compute() {

        for (int i = 0; i < output.size(); ++i) {
            int result = 0;
            for (int k = 0; k < 3; ++k) {
                result |= input[i * 3 + k];
            }
            output[i] = result;
        }

        return true;
    }
};








class LogicalConv3Asm: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<int>> input;
    poplar::Output<poplar::Vector<int>> output;

    bool compute() { 
        int outsize = output.size();

        asm volatile(R"(
            ld32 %[outsize], $m0, $m15, 3
            brz %[outsize], .done
            mov     %[outsize], $m15
            ld32 $m3, $m0, $m15, 0
            ld32 $m2, $m0, $m15, 2
            add $m3, $m3, 8
    .step_window:
            add $m4, $m15, -8
            add $m5, $m15, -4
            ld32 $m4, $m3, $m4, 0
            ld32 $m5, $m3, $m5, 0
            or $m4, $m5, $m4
            ld32step $m5, $m15, $m3+=, 3
            or $m4, $m5, $m4
            st32step $m4, $m15, $m2+=, 1
            add %[outsize], %[outsize], 1
            ld32 $m4, $m0, $m15, 3
            cmpult $m4, %[outsize], $m4
            brnz $m4, .step_window
    .done:
        )" : 
        [outsize] "+r"(outsize) ::
        );

        return true;
    }
};







class LogicalConv3: public poplar::Vertex {
    public:
    poplar::InOut<poplar::Vector<int>> input;
    poplar::Output<poplar::Vector<int>> output;

    bool compute() { 

        #pragma IPUpy_start [X=input, output]

        for i in range(0, len(output)):
            output[i] = any(X[3 * i : 3 * (i + 1)])


        #pragma IPUpy_end

        return true;
    }
};


