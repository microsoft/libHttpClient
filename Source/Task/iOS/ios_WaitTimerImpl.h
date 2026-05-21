//
//  ios_WaitTimerImpl.h
//  libHttpClient
//
//  Created by Brian Pepin on 9/11/18.
//

#ifndef ios_WaitTimerImpl_h
#define ios_WaitTimerImpl_h

#include "../WaitTimer.h"
#include "ios_WaitTimer_target.h"

namespace OS
{

class WaitTimerImpl
{
public:
    WaitTimerImpl();
    ~WaitTimerImpl();
    HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback);
    void Start(_In_ uint64_t dueTime);
    void Cancel();
    void TimerFired();
    
private:
    void* m_context;
    WaitTimerCallback* m_callback;
    ios_WaitTimer_target* m_target;
    NSTimer* m_timer;
};

} // namespace OS



#endif /* ios_WaitTimerImpl_h */
