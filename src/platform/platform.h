#ifndef OPENKICK_PLATFORM_H
#define OPENKICK_PLATFORM_H

#include "ui/editor.h"

typedef struct PlatformView PlatformView;

PlatformView* platformViewAttach(void* parent, Editor* editor, float scale);
void platformViewSetScale(PlatformView* view, float scale);
void platformViewDetach(PlatformView* view);
void platformModuleExit(void);

#endif
