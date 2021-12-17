#ifndef EDK_INTERNAL_CNRT_WRAP_H_
#define EDK_INTERNAL_CNRT_WRAP_H_

#include <cnrt.h>

// cnrt changed api name at v5.0.0
namespace cnrt {
#if CNRT_MAJOR_VERSION < 5
// cnrtQueue_t
inline cnrtRet_t QueueCreate(cnrtQueue_t *pqueue) { return cnrtCreateQueue(pqueue); }
inline cnrtRet_t QueueDestroy(cnrtQueue_t queue) { return cnrtDestroyQueue(queue); }
inline cnrtRet_t QueueSync(cnrtQueue_t queue) { return cnrtSyncQueue(queue); }
// cnrtNotifier_t
inline cnrtRet_t NotifierCreate( cnrtNotifier_t *pn) { return cnrtCreateNotifier(pn); }
inline cnrtRet_t NotifierDestroy(cnrtNotifier_t n) { return cnrtDestroyNotifier(&n); }
inline cnrtRet_t NotifierDuration(cnrtNotifier_t start, cnrtNotifier_t end, float* dura) {
  return cnrtNotifierDuration(start, end, dura);
}
#else
// cnrtQueue_t
inline cnrtRet_t QueueCreate(cnrtQueue_t *pqueue) { return cnrtQueueCreate(pqueue); }
inline cnrtRet_t QueueDestroy(cnrtQueue_t queue) { return cnrtQueueDestroy(queue); }
inline cnrtRet_t QueueSync(cnrtQueue_t queue) { return cnrtQueueSync(queue); }
// cnrtNotifier_t
inline cnrtRet_t NotifierCreate( cnrtNotifier_t *pn) { return cnrtNotifierCreate(pn); }
inline cnrtRet_t NotifierDestroy(cnrtNotifier_t n) { return cnrtNotifierDestroy(n); }
inline cnrtRet_t NotifierDuration(cnrtNotifier_t start, cnrtNotifier_t end, float* dura) {
  return cnrtNotifierElapsedTime(start, end, dura);
}
#endif

inline cnrtRet_t PlaceNotifier(cnrtNotifier_t notifier, cnrtQueue_t queue) {
  return cnrtPlaceNotifier(notifier, queue);
}

}  // namespace cnrt

#endif  // EDK_INTERNAL_CNRT_WRAP_H_
