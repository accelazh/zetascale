//----------------------------------------------------------------------------
// ZetaScale
// Copyright (c) 2016, SanDisk Corp. and/or all its affiliates.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 2.1 as published by the Free
// Software Foundation;
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License v2.1 for more details.
//
// A copy of the GNU Lesser General Public License v2.1 is provided with this package and
// can also be found at: http://opensource.org/licenses/LGPL-2.1
// You should have received a copy of the GNU Lesser General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 59 Temple
// Place, Suite 330, Boston, MA 02111-1307 USA.
//----------------------------------------------------------------------------

#ifndef PHYSMEM_H
#define PHYSMEM_H 1

/*
 * File:   sdf/physmem/physmem.h
 * Author: drew
 *
 * Created on July 10, 2008
 *
 * (c) Copyright 2008, Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 * $Id: physmem.h 3568 2008-09-25 08:46:17Z drew $
 */

/**
 * physmem provides access to PCI consistent memory from user and
 * kernel space, exposing the physical mappings so that hardware
 * can be instructed to do DMAs without crossing the user-kernel
 * boundary.
 *
 * Users may wish to map each physical region separately, since
 * this will make it harder to create buffers which span physical
 * areas that will cause DMAs to main memory to overrun into incorrect
 * pages.
 */

#include <asm/ioctl.h>
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/** @brief Single memory region */
struct physmem_region {
    /** @brief Offset into mmap */
    uint64_t offset;

    /** @brief Physical mapping */
    uint64_t paddr;

    /** @brief Length of this region */
    uint64_t len;
};

/**
 * @brief Complete dfescription of memory space
 *
 * This is used as the input-output parameter retrieving the list of
 * #physmem_region objects from the kernel driver.
 */
struct physmem_regions {
    /*
     * Serial number for this mapping.  Serial numbers for a given reboot
     * are unique.
     */
    uint64_t serial;

    /**
     * @brief Total number of regions which exist.
     *
     * Output from ioctl.  May be more or less than nregion number allocated
     * in this structure.
     */
    uint32_t total_region;

    /**
     * @brief Total size of all regions.
     *
     * Output from ioctl.
     */
    uint64_t total_len;

    /**
     * @brief Allocated size of region.
     *
     * Input to ioctl.
     */
    int32_t nregion;

    /**
     * Array of regions
     *
     * Output from ioctl.
     */
    struct physmem_region region[];
};

/**
 * @brief ioctl to get mappings
 *
 * Usage:
 * @code
 * int fd = open("/dev/physmem0", O_RDWR);
 * assert(fd != -1);
 *
 * struct physmem_regions *tmp = calloc(1, sizeof(*tmp));
 * assert(tmp);
 *
 * physmem_regions->nregion = 0;
 * int status = ioctl(fd, PHYSMEMGETREGIONS, tmp);
 * assert(!status);
 *
 * tmp = realloc(sizeof (*tmp) + sizeof (*tmp->region[0]) * tmp->total_region);
 * assert(tmp);
 * tmp->nregion = tmp->total_region;
 *
 * int status = ioctl(fd, PHYSMEMGETREGIONS, tmp);
 * assert(!status);
 * @endcode
 */
#define PHYSMEMGETREGIONS _IOWR(0xCC, 0, struct physmem_regions)

#endif /* ndef PHYSMEM_H */
