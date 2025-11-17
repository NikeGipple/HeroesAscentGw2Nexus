// MumbleLink.h
#pragma once
#ifndef MUMBLELINK_H
#define MUMBLELINK_H

#include "Mumble/Mumble.h"

namespace MumbleLink
{
    extern Mumble::Data* gMumble;

    void Init();

    inline const Mumble::Data* GetData() {
        return gMumble;
    }
}


#endif
