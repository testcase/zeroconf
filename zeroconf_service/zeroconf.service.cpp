/**
 */

#include "ext.h"
#include "ext_obex.h"
#include "../zeroconf/NetService.h"
#include "../zeroconf/NetServiceThread.h"
#include <iostream>

// a macro to mark exported symbols in the code without requiring an external file to define them
#ifdef WIN_VERSION
// note that this is the required syntax on windows regardless of whether the compiler is msvc or gcc
#define T_EXPORT __declspec(dllexport)
#else // MAC_VERSION
// the mac uses the standard gcc syntax, you should also set the -fvisibility=hidden flag to hide the non-marked symbols
#define T_EXPORT __attribute__((visibility("default")))
#endif

using namespace ZeroConf;

class Service;

struct zeroconf_service
{
    t_object ob;			// the object itself (must be first)
    t_symbol *name;
    t_symbol *type;
    t_symbol *domain;
    t_symbol *interface;
    void *clock;
    long port;
    Service *mpService;
};

class Service : public NetService, public NetServiceListener
{
    zeroconf_service *mpExternal;
public:
    Service(const std::string &domain,
            const std::string &type,
            const std::string &name,
            const std::string &interface,
            const int port,
            zeroconf_service *x)
    : NetService(domain, type, name, interface, port)
    , mpExternal(x)
    {
        setListener(this);
    }
    
private:
    virtual void willPublish(NetService *pNetService) {}
    virtual void didNotPublish(NetService *pNetService) { object_post((t_object *)mpExternal, "didNotPublish" ); }
    virtual void didPublish(NetService *pNetService)
    {
        object_post((t_object *)mpExternal, "Service published: %s %d", pNetService->getName().c_str(), pNetService->getPort());
    }
    
    virtual void willResolve(NetService *pNetService) {}
    virtual void didNotResolve(NetService *NetService) {}
    virtual void didResolveAddress(NetService *pNetService) {}
    virtual void didUpdateTXTRecordData(NetService *pNetService) {}
    virtual void didStop(NetService *pNetService) {}
};

//------------------------------------------------------------------------------
t_class *zeroconf_service_class;

void zeroconf_service_poll(zeroconf_service *x, t_symbol *sym, short argc, t_atom *arv)
{
    // poll for results
    if(!NOGOOD(x)) {
        if(x->mpService && x->mpService->getDNSServiceRef())
        {
            DNSServiceErrorType err = kDNSServiceErr_NoError;
            if(NetServiceThread::poll(x->mpService->getDNSServiceRef(), 0.001, err))
            {
                if(err > 0)
                {
                    x->mpService->stop();
                    object_post((t_object*)x, "error %d", err);
                }
            }
            else
            {
                if(x->mpService && x->mpService->getDNSServiceRef()) // we check again, because it might have change in reaction to a callback
                {
                     clock_delay(x->clock, 1000);
                }
            }
        }
    }
}

void zeroconf_service_bang(zeroconf_service *x)
{
    if(x->mpService)
    {
        delete x->mpService;
        x->mpService = NULL;
    }
    
    x->mpService = new Service(x->domain->s_name,
                               x->type->s_name,
                               x->name->s_name,
                               x->interface->s_name,
                               x->port,
                               x);
    x->mpService->publish(false);

}

void zeroconf_service_assist(zeroconf_service *x, void *b, long m, long a, char *s)
{
    if (m == ASSIST_INLET)
    {
        sprintf(s, "I am inlet %ld", a);
    }
    else
    {
        sprintf(s, "I am outlet %ld", a);
    }
}

void zeroconf_service_free(zeroconf_service *x)
{
    if(x->mpService)
    {
        delete x->mpService;
    }
}

void *zeroconf_service_new(t_symbol *s, long argc, t_atom *argv)
{
    zeroconf_service *x = NULL;
    
    if ((x = (zeroconf_service *)object_alloc(zeroconf_service_class)))
    {
        x->mpService = NULL;
        x->name = gensym("");
        x->type = gensym("");
        x->domain = gensym("");
        x->interface = gensym("");
        x->port = 0;
        x->clock = clock_new((t_object *)x, (method)zeroconf_service_poll);
        attr_args_process(x, argc, argv);
        
        
        clock_delay(x->clock, 1000);

    }
    
    return (x);
}

int T_EXPORT main(void)
{
    t_class *c = class_new("zeroconf.service", (method)zeroconf_service_new, (method)zeroconf_service_free, (long)sizeof(zeroconf_service), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)zeroconf_service_bang,			"bang",	0);
    class_addmethod(c, (method)zeroconf_service_assist,			"assist",	A_CANT, 0);
    
    CLASS_ATTR_SYM(c, "name", 0, zeroconf_service, name);
    CLASS_ATTR_SYM(c, "type", 0, zeroconf_service, type);
    CLASS_ATTR_SYM(c, "domain", 0, zeroconf_service, domain);
    CLASS_ATTR_LONG(c, "port", 0, zeroconf_service, port);
    CLASS_ATTR_SYM(c, "interface", 0, zeroconf_service, interface);
    
    class_register(CLASS_BOX, c); /* CLASS_NOBOX */
    zeroconf_service_class = c;
    
    post("| zeroconf.service - code by Remy Muller");
    post("| This maintenance release by Nick Rothwell, nick@cassiel.com");
    post("| $Id: zeroconf.service.cpp,v cfa525e1c723 2011/02/26 22:01:01 nick $");
    
    return 0;
}
