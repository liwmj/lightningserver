#include "session.hxx"
#include "sessioninfo.hxx"
#include "userrequest.hxx"
#include "datastreamhandler.hxx"
#include "datahandlerfactory.hxx"

#include "util/logger.hxx"

#include <boost/bind.hpp>
#include <boost/noncopyable.hpp>

using namespace Lightning;
//-------------------------------------------------
struct FreeBufferEvent
{
    void operator()(bufferevent* be)
    {
        if (NULL != be)
        {
            bufferevent_free(be);
            be = NULL;
        }
    }
};
//-------------------------------------------------
class Session::SessionUtil
:public boost::noncopyable
{
    public:
        static void onBufferEventRead(bufferevent* bev, void* ctx)
        {
            Session* me = static_cast<Session*>(ctx);
            if (me)
            {
                evbuffer* input = bufferevent_get_input(bev);
                if (NULL != input &&
                            me->mDataStreamHandler)
                {
                    me->mDataStreamHandler->pushData(input);
                }
            }
        }

        static void onBufferEventWrite(bufferevent* bev, void* ctx)
        {
            //TODO:
        }

        static void onBuffereventEvent(bufferevent* bev, short what, void* ctx)
        {
            Session* me = static_cast<Session*>(ctx);
            if (me)
            {
                if (what & BEV_EVENT_READING &&
                            what & BEV_EVENT_EOF)
                {
                    INFO(__FUNCTION__ << " | client fd close");
                    me->OnClientFdClosed(me->shared_from_this());
                }
                else if (what & BEV_EVENT_ERROR)
                {
                    INFO(__FUNCTION__ << " | Error");
                    me->OnError(me->shared_from_this(), SEC_ERROR);
                }
                else if (what & BEV_EVENT_WRITING &&
                            what & BEV_EVENT_TIMEOUT)
                {
                    INFO(__FUNCTION__ << " | Timeout to Write");
                    me->OnError(me->shared_from_this(), SEC_WRITE_TIMEOUT);
                }
                else if (what & BEV_EVENT_READING &&
                            what & BEV_EVENT_TIMEOUT)
                {
                    INFO(__FUNCTION__ << " | Timeout to read");
                    me->OnError(me->shared_from_this(), SEC_READ_TIMEOUT);
                }
            }
        }
};
//-------------------------------------------------
Session::Session(boost::weak_ptr<UserRequestFactory> mRequestFactory,
            evutil_socket_t fd,
            const char* ip)
:mUserRequestFactory(mRequestFactory)
,mSessionInfo(new SessionInfo(fd, ip))
,mSessionId(0)
{
}

Session::~Session()
{
}

const Session::SessionIdType Session::getSessionId()
{
    SessionIdType id = 0;
    if (mSessionInfo)
    {
        id = mSessionInfo->mClientFd;
    }
    return id;
}

bool Session::init(event_base* eb)
{
    bool rt = false;

    if (NULL != eb &&
                mSessionInfo)
    {
        mBufferEvent.reset(bufferevent_socket_new(eb,
                        mSessionInfo->mClientFd,
                        0/*BEV_OPT_UNLOCK_CALLBACKS | BEV_OPT_DEFER_CALLBACKS*/),
                    FreeBufferEvent());
        if (NULL != mBufferEvent)
        {
            bufferevent_setcb(mBufferEvent.get(),
                        SessionUtil::onBufferEventRead,
                        SessionUtil::onBufferEventWrite,
                        SessionUtil::onBuffereventEvent,
                        this);
            initDataHandler();
            rt = true;
        }
        else
        {
            ERROR(__FUNCTION__ << " | failed to bufferevent_socket_new");
        }
    }

    return rt;
}

void Session::initDataHandler()
{
    UserRequestFactoryPtrType userRequestFactory = mUserRequestFactory.lock();
    if (userRequestFactory)
    {
        UserRequestPtrType userRequest = userRequestFactory->create();
        if (userRequest)
        {
            mDataStreamHandler = DataHandlerFactory::create(userRequest->getType());
            if (mDataStreamHandler)
            {
                mDataStreamHandler->setUserRequest(userRequest);
                mDataStreamHandler->OnRecvRequestFinished = boost::bind(
                            &Session::onRecvRequestFinished,
                            this,
                            _1,
                            _2);
            }
        }
    }
}

void Session::startAction(short what)
{
    if (NULL != mBufferEvent)
    {
        bufferevent_enable(mBufferEvent.get(), what);
    }
}

void Session::sendData(const char* data, size_t length)
{
    if (NULL != data &&
                length > 0)
    {
        bufferevent_write(mBufferEvent.get(),
                    data,
                    length);
    }
}
//---------------------------------------
void Session::onRecvRequestFinished(DataHandler*, UserRequestPtrType request)
{
    DEBUG(__FUNCTION__);
    OnRecvRequestFinished(shared_from_this(), request);
    
    UserRequestFactoryPtrType userRequestFactory = mUserRequestFactory.lock();
    if (userRequestFactory &&
                mDataStreamHandler)
    {
        UserRequestPtrType userRequest = userRequestFactory->create();
        if (userRequest)
        {
            mDataStreamHandler->setUserRequest(userRequest);
        }
    }
}
//---------------------------------------
std::ostream& Session::toString(std::ostream& os) const
{
    return os << " | Session ["
        << " socket : " << mSessionInfo->mClientFd
        << " | Ip : " << mSessionInfo->mClientIp;
}
//-------------------------------------------------
std::ostream& Lightning::operator << (std::ostream& os, const Session& session)
{
    return session.toString(os);
}
//-------------------------------------------------