// debug_host.cxx - arena size & init failure diagnosis
#include <stdio.h>
#include <string.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/recording_micro_interpreter.h"
#include "tensorflow/lite/micro/recording_micro_allocator.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model/model_data.h"

int main(void)
{
  const tflite::Model* model = tflite::GetModel(g_velaai_model);
  if (model == nullptr) { printf("GetModel failed\n"); return 1; }
  printf("schema version: %d (expect %d)\n", model->version(), TFLITE_SCHEMA_VERSION);

  static tflite::MicroMutableOpResolver<12> resolver;
  resolver.AddGather();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddSplit();
  resolver.AddUnpack();
  resolver.AddTanh();
  resolver.AddLogistic();
  resolver.AddMul();
  resolver.AddAdd();
  resolver.AddQuantize();
  resolver.AddDequantize();
  printf("resolver ok\n");

  constexpr size_t kArena = 512 * 1024;
  static uint8_t arena[kArena] __attribute__((aligned(16)));

  auto* allocator = tflite::RecordingMicroAllocator::Create(arena, kArena);
  tflite::RecordingMicroInterpreter interp(model, resolver, allocator);
  TfLiteStatus st = interp.AllocateTensors();
  printf("AllocateTensors: %d\n", (int)st);
  printf("arena used: %zu bytes (of %zu)\n", interp.arena_used_bytes(), kArena);
  interp.GetMicroAllocator().PrintAllocations();
  return st == kTfLiteOk ? 0 : 1;
}
