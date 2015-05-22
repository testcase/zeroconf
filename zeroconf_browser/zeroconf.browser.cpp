/*
	Copyright (c) 2009 Remy Muller.
	
	Permission is hereby granted, free of charge, to any person obtaining
	a copy of this software and associated documentation files
	(the "Software"), to deal in the Software without restriction,
	including without limitation the rights to use, copy, modify, merge,
	publish, distribute, sublicense, and/or sell copies of the Software,
	and to permit persons to whom the Software is furnished to do so,
	subject to the following conditions:
	
	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.
	
	Any person wishing to distribute modifications to the Software is
	requested to send the modifications to the original developer so that
	they can be incorporated into the canonical version.
	
	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
	ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
	CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
	WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ext.h"
#include "ext_obex.h"
#include "../zeroconf/NetServiceBrowser.h"
#include "../zeroconf/NetService.h"
#include "../zeroconf/NetServiceThread.h"
#include <iostream>
#include <string>
#include <set>

// a macro to mark exported symbols in the code without requiring an external file to define them
#ifdef WIN_VERSION
// note that this is the required syntax on windows regardless of whether the compiler is msvc or gcc
#define T_EXPORT __declspec(dllexport)
#else // MAC_VERSION
// the mac uses the standard gcc syntax, you should also set the -fvisibility=hidden flag to hide the non-marked symbols
#define T_EXPORT __attribute__((visibility("default")))
#endif

using namespace ZeroConf;

class Browser;

struct zeroconf_browser
{
    t_object ob;			// the object itself (must be first)
    t_symbol *type;
    t_symbol *domain;
    void *out;
    void *clock;
    Browser *mpBrowser;
};

class Browser : public NetServiceBrowser, public NetServiceBrowserListener
{
    zeroconf_browser *mpExternal;
    std::set<std::string> mServices;
    CriticalSection mCriticalSection; // CriticalSection for shared access to mServices
    
public:
    Browser(zeroconf_browser *x)
    : NetServiceBrowser()
    , mpExternal(x)
    {
        setListener(this);
    }
    
    void search(const std::string &type, const std::string &domain, bool launchThread)
    {
        if(!type.empty())
        {
            stop();
            {
               ScopedLock lock(mCriticalSection);
                mServices.clear();
            }
            searchForServicesOfType(type, domain, launchThread);
        }
    }
    
private:
    virtual void didFindDomain(NetServiceBrowser *pNetServiceBrowser, const std::string &domainName, bool moreDomainsComing) { }
    virtual void didRemoveDomain(NetServiceBrowser *pNetServiceBrowser, const std::string &domainName, bool moreDomainsComing) { }
    
    void outputListOfServices()
    {
        t_atom at[1];
        outlet_anything(mpExternal->out, gensym("clear"), 0, NULL);
        
      ScopedLock lock(mCriticalSection);
        for(std::set<std::string>::iterator it=mServices.begin(); it != mServices.end(); ++it)
        {
            atom_setsym(at, gensym(const_cast<char*>(it->c_str())));
            outlet_anything(mpExternal->out, gensym("append"), 1, at);
        }
    }
    
    virtual void didFindService(NetServiceBrowser* pNetServiceBrowser, NetService *pNetService, bool moreServicesComing)
    {
        {
           ScopedLock lock(mCriticalSection);
            mServices.insert(pNetService->getName());
        }
        
        if(!moreServicesComing)
            outputListOfServices();
    }
    
    virtual void didRemoveService(NetServiceBrowser *pNetServiceBrowser, NetService *pNetService, bool moreServicesComing)
    {
        bool erased = false;
        {
            std::set<std::string>::iterator it = mServices.find(pNetService->getName());
            if(it != mServices.end())
            {
                erased = true;
                ScopedLock lock(mCriticalSection);
                mServices.erase(it);
            }
        }
        
        //if(!moreServicesComing) // doesn't seem to be ever set to false
        if(erased)
            outputListOfServices();
    }
    
    virtual void willSearch(NetServiceBrowser *pNetServiceBrowser) { }
    virtual void didNotSearch(NetServiceBrowser *pNetServiceBrowser) { }
    virtual void didStopSearch(NetServiceBrowser *pNetServiceBrowser) { }
};

//------------------------------------------------------------------------------
t_class *zeroconf_browser_class;

void zeroconf_browser_poll(zeroconf_browser *x)
{
    // poll for results
    if(!NOGOOD(x)) {
        if(x->mpBrowser->getDNSServiceRef())
        {
            DNSServiceErrorType err = kDNSServiceErr_NoError;
            if(NetServiceThread::poll(x->mpBrowser->getDNSServiceRef(), 0.001, err) && err > 0)
            {
                object_post((t_object*)x, "error %d", err);
                x->mpBrowser->stop();
            }
            else if(x->mpBrowser->getDNSServiceRef()) // we check again, because it might have change in reaction to a callback
            {
                //schedule_defer(x, (method)zeroconf_browser_poll, 1000, NULL, 0, NULL); // reschedule in 1 sec
                clock_delay(x->clock, 1000);
                
            }
        }
    }
}

void zeroconf_browser_bang(zeroconf_browser *x)
{
    x->mpBrowser->search(x->type->s_name, x->domain->s_name, false);
}

void zeroconf_browser_browse(zeroconf_browser *x, t_symbol *s, long argc, t_atom *argv)
{
    bool valid = false;
    switch(argc)
    {
        case 2:
            if(argv[1].a_type == A_SYM)
            {
                x->domain = atom_getsym(argv+1);
            }
        case 1:
            if(argv[0].a_type == A_SYM)
            {
                valid = true;
                x->type = atom_getsym(argv+0);
            }
        default:
            break;
    }
    
    if(valid)
        zeroconf_browser_bang(x);
}

void zeroconf_browser_assist(zeroconf_browser *x, void *b, long m, long a, char *s)
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

void zeroconf_browser_free(zeroconf_browser *x)
{
    clock_unset(x->clock);
    if(x->mpBrowser)
    {
        delete x->mpBrowser;
    }
}

void *zeroconf_browser_new(t_symbol *s, long argc, t_atom *argv)
{
    zeroconf_browser *x = NULL;
    
    if ((x = (zeroconf_browser *)object_alloc(zeroconf_browser_class)))
    {
        x->out = outlet_new(x, NULL);
        x->type = gensym("");
        x->domain = gensym("local.");
        x->mpBrowser = new Browser(x);
        x->clock = clock_new((t_object *)x, (method)zeroconf_browser_poll);
        attr_args_process(x, argc, argv);
        
        x->mpBrowser->search(x->type->s_name, x->domain->s_name, false);
        clock_delay(x->clock, 1000);
    }
    return (x);
}

int T_EXPORT main(void)
{
    t_class *c = class_new("zeroconf.browser", (method)zeroconf_browser_new, (method)zeroconf_browser_free, (long)sizeof(zeroconf_browser), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)zeroconf_browser_bang,       "bang",		0);
    class_addmethod(c, (method)zeroconf_browser_browse,			"browse",	A_GIMME, 0);
    class_addmethod(c, (method)zeroconf_browser_assist,			"assist",	A_CANT, 0);  
    
    CLASS_ATTR_SYM(c, "type", 0, zeroconf_browser, type);
    CLASS_ATTR_SYM(c, "domain", 0, zeroconf_browser, domain);
    
    class_register(CLASS_BOX, c); /* CLASS_NOBOX */
    zeroconf_browser_class = c;
    
    post("| zeroconf.browser - code by Remy Muller");
    post("| This maintenance release by Nick Rothwell, nick@cassiel.com");
    post("| $Id: zeroconf.browser.cpp,v cfa525e1c723 2011/02/26 22:01:01 nick $");
    
    return 0;
}
