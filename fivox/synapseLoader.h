/* Copyright (c) 2015-2016, EPFL/Blue Brain Project
 *                          Stefan.Eilemann@epfl.ch
 *
 * This file is part of Fivox <https://github.com/BlueBrain/Fivox>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef FIVOX_SYNAPSELOADER_H
#define FIVOX_SYNAPSELOADER_H

#include <fivox/api.h>
#include <fivox/eventSource.h> // base class

namespace fivox
{
/** Loads BBP synapse files to be sampled by an EventFunctor. */
class SynapseLoader : public EventSource
{
public:
    /**
    * Construct a new synapse event source.
    *
    * @param params the URIHandler object containing the parameters
    * to define the event source
    * @throw H5::exception or std::exception on error
    */
    FIVOX_API explicit SynapseLoader(const URIHandler& params);
    FIVOX_API virtual ~SynapseLoader();

private:
    /** @name Abstract interface implementation */
    //@{
    Vector2f _getTimeRange() const final;
    ssize_t _load(size_t chunkIndex, size_t numChunks) final;
    SourceType _getType() const final { return SourceType::frame; }
    size_t _getNumChunks() const final;
    //@}

    class Impl;
    std::unique_ptr<Impl> _impl;
};
}

#endif
