#if __cplusplus
namespace sandook {
#endif

enum IOStatus { kOk = 0, kFailed = 1 };

struct IOResult {
  enum IOStatus status;
  int res;
};

#if __cplusplus
}  // namespace sandook
#endif