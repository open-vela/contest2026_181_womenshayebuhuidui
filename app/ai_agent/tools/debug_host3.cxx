// debug_host3.cxx - run inference step by step with error visibility
#include <stdio.h>
#include <string.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model/model_data.h"
#include "tokenizer.h"
#include "ai_lm.h"

int main(void)
{
  const tflite::Model* model = tflite::GetModel(g_velaai_model);
  static tflite::MicroMutableOpResolver<12> resolver;
  resolver.AddGather(); resolver.AddFullyConnected(); resolver.AddSoftmax();
  resolver.AddSplit(); resolver.AddUnpack(); resolver.AddTanh();
  resolver.AddLogistic(); resolver.AddMul(); resolver.AddAdd();
  resolver.AddQuantize(); resolver.AddDequantize();
  constexpr size_t kArena = 64 * 1024;
  static uint8_t arena[kArena] __attribute__((aligned(16)));
  static tflite::MicroInterpreter interp(model, resolver, arena, kArena);
  if (interp.AllocateTensors() != kTfLiteOk) { printf("alloc fail\n"); return 1; }
  printf("alloc ok\n");

  const char* prompt = "<usr> 你好 <bot>";
  int32_t ids[64];
  int n = sp_tokenize(prompt, ids, 64);
  printf("prompt tokens (%d): ", n);
  for (int i = 0; i < n; i++) printf("%d ", ids[i]);
  printf("\n");

  float c[256] = {0}, h[256] = {0};
  TfLiteTensor* in_tok = interp.input(0);
  TfLiteTensor* in_h = interp.input(1);
  TfLiteTensor* in_c = interp.input(2);
  for (int i = 0; i < n; i++) {
    in_tok->data.i32[0] = ids[i];
    memcpy(in_h->data.f, h, sizeof(h));
    memcpy(in_c->data.f, c, sizeof(c));
    TfLiteStatus st = interp.Invoke();
    if (st != kTfLiteOk) { printf("INVOKE FAIL at prompt step %d (st=%d)\n", i, st); return 1; }
    memcpy(c, interp.output(0)->data.f, sizeof(c));
    memcpy(h, interp.output(2)->data.f, sizeof(h));
  }
  printf("prompt encoding done\n");

  int32_t out[32]; int outlen = 0;
  for (int step = 0; step < 16; step++) {
    const TfLiteTensor* logits = interp.output(1);
    int best = 0;
    for (int i = 1; i < 5000; i++) if (logits->data.f[i] > logits->data.f[best]) best = i;
    printf("step %d argmax=%d", step, best);
    if (best == 2) { printf(" (EOS)\n"); break; }
    out[outlen++] = best;
    in_tok->data.i32[0] = best;
    memcpy(in_h->data.f, h, sizeof(h));
    memcpy(in_c->data.f, c, sizeof(c));
    TfLiteStatus st = interp.Invoke();
    if (st != kTfLiteOk) { printf(" INVOKE FAIL (st=%d)\n", st); return 1; }
    memcpy(c, interp.output(0)->data.f, sizeof(c));
    memcpy(h, interp.output(2)->data.f, sizeof(h));
    printf(" (ok)\n");
  }
  char dec[512];
  sp_detokenize(out, outlen, dec, sizeof(dec));
  printf("generated: %s\n", dec);
  return 0;
}
