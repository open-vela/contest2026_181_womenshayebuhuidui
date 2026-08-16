// debug_host2.cxx - dump model ops & tensor types, then try allocate
#include <stdio.h>
#include <string.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/recording_micro_interpreter.h"
#include "tensorflow/lite/micro/recording_micro_allocator.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model/model_data.h"

static const char* BuiltinName(int code) {
  switch (code) {
    case tflite::BuiltinOperator_ADD: return "ADD";
    case tflite::BuiltinOperator_DEQUANTIZE: return "DEQUANTIZE";
    case tflite::BuiltinOperator_FULLY_CONNECTED: return "FULLY_CONNECTED";
    case tflite::BuiltinOperator_GATHER: return "GATHER";
    case tflite::BuiltinOperator_LOGISTIC: return "LOGISTIC";
    case tflite::BuiltinOperator_MUL: return "MUL";
    case tflite::BuiltinOperator_QUANTIZE: return "QUANTIZE";
    case tflite::BuiltinOperator_SOFTMAX: return "SOFTMAX";
    case tflite::BuiltinOperator_SPLIT: return "SPLIT";
    case tflite::BuiltinOperator_TANH: return "TANH";
    case tflite::BuiltinOperator_UNPACK: return "UNPACK";
    case tflite::BuiltinOperator_UNIDIRECTIONAL_SEQUENCE_LSTM: return "LSTM";
    default: return "?";
  }
}

int main(void)
{
  const tflite::Model* model = tflite::GetModel(g_velaai_model);
  const tflite::SubGraph* sg = model->subgraphs()->Get(0);
  printf("nodes: %zu, tensors: %zu\n", sg->operators()->size(), sg->tensors()->size());
  auto* ops = model->operator_codes();
  for (size_t i = 0; i < sg->operators()->size(); i++) {
    const tflite::Operator* op = sg->operators()->Get(i);
    const tflite::OperatorCode* code = ops->Get(op->opcode_index());
    printf("node %zu: %s (deprecated=%d)\n", i,
           BuiltinName(code->builtin_code()), code->deprecated_builtin_code());
  }
  printf("inputs: ");
  for (size_t i = 0; i < sg->inputs()->size(); i++) printf("%d ", sg->inputs()->Get(i));
  printf("\noutputs: ");
  for (size_t i = 0; i < sg->outputs()->size(); i++) printf("%d ", sg->outputs()->Get(i));
  printf("\n");
  return 0;
}
