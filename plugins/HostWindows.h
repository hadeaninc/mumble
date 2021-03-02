// Copyright 2020-2021 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef HOSTWINDOWS_H_
#define HOSTWINDOWS_H_

#include "Module.h"

typedef uint32_t procid_t;

class HostWindows {
protected:
	procid_t m_pid;

public:
	bool peek(const procptr_t address, void *dst, const size_t size) const;
	Modules modules() const;

	HostWindows(const procid_t pid);
	virtual ~HostWindows();
};

#endif
