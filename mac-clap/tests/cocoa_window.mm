#import <Cocoa/Cocoa.h>
#include <clap/clap.h>
#include <cstdio>

bool testCocoaWindow(const clap_plugin_t* plug,const clap_plugin_gui_t& gui)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1352,708)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        NSView* parent=window.contentView;
        clap_window_t target {};
        target.api=CLAP_WINDOW_API_COCOA;
        target.cocoa=parent;
        const bool created=gui.create(plug,target.api,false);
        bool ok=created && gui.set_size(plug,1352,708) && gui.set_parent(plug,&target) && gui.show(plug);
        NSView* child=parent.subviews.lastObject;
        ok=ok && child && NSEqualSizes(child.bounds.size,NSMakeSize(1352,708));
        [window setContentSize:NSMakeSize(960,540)];
        ok=ok && gui.set_size(plug,960,540) && NSEqualSizes(child.bounds.size,NSMakeSize(960,540));
        ok=ok && gui.hide(plug) && child.hidden && gui.show(plug) && !child.hidden;
        if (created) gui.destroy(plug);
        ok=ok && parent.subviews.count==0;
        [window release];
        if (!ok) std::fprintf(stderr,"FAIL: Cocoa CLAP child creation, resize or show/hide\n");
        return ok;
    }
}
