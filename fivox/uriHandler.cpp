/* Copyright (c) 2014-2016, EPFL/Blue Brain Project
 *                          Stefan.Eilemann@epfl.ch
 *                          Jafet.VillafrancaDiaz@epfl.ch
 *                          Daniel.Nachbaur@epfl.ch
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

#include "uriHandler.h"

#include <fivox/compartmentLoader.h>
#include <fivox/densityFunctor.h>
#include <fivox/fieldFunctor.h>
#include <fivox/frequencyFunctor.h>
#ifdef FIVOX_USE_LFP
#  include <fivox/lfp/lfpFunctor.h>
#endif
#include <fivox/imageSource.h>
#include <fivox/somaLoader.h>
#include <fivox/spikeLoader.h>
#include <fivox/synapseLoader.h>
#include <fivox/testLoader.h>
#include <fivox/vsdLoader.h>
#ifdef FIVOX_USE_BBPTESTDATA
#  include <BBP/TestDatasets.h>
#endif
#include <lunchbox/file.h>
#include <lunchbox/log.h>
#include <lunchbox/uri.h>
#include <boost/lexical_cast.hpp>
#include <fivox/itk.h>

#include <brion/blueConfig.h>
#include <brain/circuit.h>

namespace fivox
{
namespace
{
using boost::lexical_cast;
const float _duration = 10.0f;
const float _dt = -1.0f; // loaders use experiment/report dt
const size_t _maxBlockSize = LB_64MB;
const float _resolution = 1.0f; // voxels per unit
const float _cutoff = 100.0f; // micrometers
const float _extend = 0.f; // micrometers
const float _gidFraction = 1.f;

EventSourcePtr _newLoader( const URIHandler& data )
{
    switch( data.getType( ))
    {
    case VolumeType::compartments:
        return std::make_shared< CompartmentLoader >( data );
    case VolumeType::somas:
        return std::make_shared< SomaLoader >( data );
    case VolumeType::spikes:
        return std::make_shared< SpikeLoader >( data );
    case VolumeType::synapses:
        return std::make_shared< SynapseLoader >( data );
    case VolumeType::test:
        return std::make_shared< TestLoader >( data );
    case VolumeType::vsd:
        return std::make_shared< VSDLoader >( data );
    default:
        return nullptr;
    }
}

template< class T > std::shared_ptr< EventFunctor< itk::Image< T, 3 >>>
_newFunctor( const URIHandler& data )
{
    switch( data.getFunctorType( ))
    {
    case FunctorType::density:
        return std::make_shared< DensityFunctor< itk::Image< T, 3 >>>
                                                       ( data.getInputRange( ));
    case FunctorType::field:
        return std::make_shared< FieldFunctor< itk::Image< T, 3 >>>
                                                       ( data.getInputRange( ));
    case FunctorType::frequency:
        return std::make_shared< FrequencyFunctor< itk::Image< T, 3 >>>
                                                       ( data.getInputRange( ));
#ifdef FIVOX_USE_LFP
    case FunctorType::lfp:
        return std::make_shared< LFPFunctor< itk::Image< T, 3 >>>
                                                       ( data.getInputRange( ));
#endif
    case FunctorType::unknown:
    default:
        return nullptr;
    }
}
}

class URIHandler::Impl
{
public:
    explicit Impl( const URI& parameters )
        : uri( parameters )
        , useTestData( false )
    {
        if( getType() == VolumeType::test )
            return;

#ifdef FIVOX_USE_BBPTESTDATA
        useTestData = uri.getPath().empty( );
        config.reset( new brion::BlueConfig( useTestData ? BBP_TEST_BLUECONFIG3
                                                         : uri.getPath( )));
#else
        config.reset( new brion::BlueConfig( uri.getPath( )));
#endif

        const brain::Circuit circuit( *config );
        const std::string target = _get( "target", useTestData ? "mini50"
                                                 : config->getCircuitTarget( ));
        const float gidFraction = getGIDFraction();
        if( target == "*" )
        {
            gids = gidFraction == 1.f ? circuit.getGIDs()
                                      : circuit.getRandomGIDs( gidFraction );
        }
        else
        {
            gids = gidFraction == 1.f ? circuit.getGIDs( target )
                                 : circuit.getRandomGIDs( gidFraction, target );
        }

        if( gids.empty( ))
            LBTHROW( std::runtime_error(
                     "No GIDs found for requested target '" + target + "'" ));
    }

    const brion::BlueConfig& getConfig() const
    {
        if( !config )
            LBTHROW( std::runtime_error(
                     "BlueConfig was not loaded" ));

        return *config;
    }

    const brion::GIDSet& getGIDs() const
    {
        if( !config )
            LBTHROW( std::runtime_error(
                     "BlueConfig was not loaded" ));

        return gids;
    }

    std::string getReport() const
    {
        const std::string& report( _get( "report" ));
        if( report.empty( ))
        {
            switch( getType( ))
            {
            case VolumeType::somas:
                return "somas";
            default:
                return _get( "functor" ) == "lfp" ? "currents" : "voltages";
            }
        }
        return report;
    }

    float getDt() const { return _get( "dt", _dt ); }

    std::string getSpikes() const { return _get( "spikes" ); }

    float getDuration() const { return _get( "duration", _duration ); }

    Vector2f getInputRange() const
    {
        Vector2f defaultValue;
        switch( getType( ))
        {
        case VolumeType::compartments:
            if( _get( "functor" ) == "lfp" )
                defaultValue = Vector2f( -1.47e-05f, 2.25e-03f );
            else
                defaultValue =
                        useTestData ? Vector2f( -190.f, 0.f )
                                    : Vector2f( brion::MINIMUM_VOLTAGE, 0.f );
            break;
        case VolumeType::somas:
            defaultValue =
                    useTestData ? Vector2f( -15.f, 0.f )
                                : Vector2f( brion::MINIMUM_VOLTAGE, 0.f );
            break;
        case VolumeType::vsd:
            defaultValue = Vector2f( -100000.f, 300.f );
            break;
        case VolumeType::spikes:
        case VolumeType::synapses:
            defaultValue = Vector2f( 0.f, 2.f );
            break;
        default:
            defaultValue = Vector2f( 0.f, 10.f );
            break;
        }

        return Vector2f( _get( "inputMin", defaultValue[0] ),
                         _get( "inputMax", defaultValue[1] ));
    }

    std::string getDyeCurve() const { return _get( "dyecurve" ); }

    float getResolution() const
    {
        return _get( "resolution", getFunctorType() == FunctorType::density
                                   ? 0.0625f : _resolution );
    }

    size_t getMaxBlockSize() const
        { return _get( "maxBlockSize", _maxBlockSize ); }

    float getCutoffDistance() const
        { return std::max( _get( "cutoff", _cutoff ), 0.f ); }

    float getExtendDistance() const
        { return std::max( _get( "extend", _extend ), 0.f ); }

    float getGIDFraction() const
        { return _get( "gidFraction", _gidFraction ); }

    std::string getReferenceVolume() const
        { return _get( "reference" ); }

    size_t getSizeInVoxel() const
        { return _get( "size", 0 ); }

    bool showProgress() const;

    VolumeType getType() const
    {
        const std::string& scheme = uri.getScheme();
        if( scheme == "fivoxsomas" )
            return VolumeType::somas;
        if( scheme == "fivoxspikes" )
            return VolumeType::spikes;
        if( scheme == "fivoxsynapses" )
            return VolumeType::synapses;
        if( scheme == "fivoxvsd" )
            return VolumeType::vsd;
        if( scheme == "fivox" || scheme == "fivoxcompartments" )
            return VolumeType::compartments;
        if( scheme == "fivoxtest" )
            return VolumeType::test;

        LBERROR << "Unknown URI scheme: " << scheme << std::endl;
        return VolumeType::unknown;
    }

    FunctorType getFunctorType() const
    {
        const std::string& functor = _get( "functor" );
        if( functor == "density" )
            return FunctorType::density;
        if( functor == "lfp" )
            return FunctorType::lfp;
        if( functor == "field" )
            return FunctorType::field;
        if( functor == "frequency" )
            return FunctorType::frequency;

        switch( getType( ))
        {
        case VolumeType::spikes:
            return FunctorType::frequency;
        case VolumeType::synapses:
            return FunctorType::density;
        case VolumeType::compartments:
        case VolumeType::somas:
        case VolumeType::vsd:
        case VolumeType::test:
        default:
            return FunctorType::field;
        }
    }

private:
    std::string _get( const std::string& param ) const
    {
        URI::ConstKVIter i = uri.findQuery( param );
        return i == uri.queryEnd() ? std::string() : i->second;
    }

    template< class T >
    T _get( const std::string& param, const T defaultValue ) const
    {
        const std::string& value = _get( param );
        if( value.empty( ))
            return defaultValue;

        try
        {
            return lexical_cast< T >( value );
        }
        catch( boost::bad_lexical_cast& )
        {
            LBWARN << "Invalid " << param << " specified, using "
                   << defaultValue << std::endl;
            return defaultValue;
        }
    }

    const URI uri;
    bool useTestData;
    std::unique_ptr< brion::BlueConfig> config;
    brion::GIDSet gids;
};

// bool specialization: param present with no value = true
template<> bool URIHandler::Impl::_get( const std::string& param,
                                        const bool defaultValue ) const
{
    URI::ConstKVIter i = uri.findQuery( param );
    if( i == uri.queryEnd( ))
        return defaultValue;
    if( i->second.empty( ))
        return true;

    try
    {
        return lexical_cast< bool >( i->second );
    }
    catch( boost::bad_lexical_cast& )
    {
        LBWARN << "Invalid " << param << " specified, using " << defaultValue
               << std::endl;
        return defaultValue;
    }
}

bool URIHandler::Impl::showProgress() const
{
    return _get( "showProgress", false );
}

URIHandler::URIHandler( const URI& params )
    : _impl( new URIHandler::Impl( params ))
{}

URIHandler::~URIHandler()
{}

const brion::BlueConfig& URIHandler::getConfig() const
{
    return _impl->getConfig();
}

const brion::GIDSet& URIHandler::getGIDs() const
{
    return _impl->getGIDs();
}

std::string URIHandler::getReport() const
{
    return _impl->getReport();
}

float URIHandler::getDt() const
{
    return _impl->getDt();
}

std::string URIHandler::getSpikes() const
{
    return _impl->getSpikes();
}

float URIHandler::getDuration() const
{
    return _impl->getDuration();
}

Vector2f URIHandler::getInputRange() const
{
    return _impl->getInputRange();
}

std::string URIHandler::getDyeCurve() const
{
    return _impl->getDyeCurve();
}

float URIHandler::getResolution() const
{
    return _impl->getResolution();
}

size_t URIHandler::getMaxBlockSize() const
{
    return _impl->getMaxBlockSize();
}

float URIHandler::getCutoffDistance() const
{
    return _impl->getCutoffDistance();
}

float URIHandler::getExtendDistance() const
{
    return _impl->getExtendDistance();
}

VolumeType URIHandler::getType() const
{
    return _impl->getType();
}

FunctorType URIHandler::getFunctorType() const
{
    return _impl->getFunctorType();
}

std::string URIHandler::getReferenceVolume() const
{
    return _impl->getReferenceVolume();
}

size_t URIHandler::getSizeInVoxel() const
{
    return _impl->getSizeInVoxel();
}

template< class T > itk::SmartPointer< ImageSource< itk::Image< T, 3 >>>
URIHandler::newImageSource() const
{
    LBINFO << "Loading events..." << std::endl;

    itk::SmartPointer< ImageSource< itk::Image< T, 3 >>> source =
        ImageSource< itk::Image< T, 3 >>::New();
    std::shared_ptr< EventFunctor< itk::Image< T, 3 >>> functor =
        _newFunctor< T >( *this );

    EventSourcePtr loader = _newLoader( *this );

    LBINFO << loader->getNumEvents() << " events " << *this << ", dt = "
           << loader->getDt() << " ready to voxelize" << std::endl;

    if( _impl->showProgress( ))
        source->showProgress();

    functor->setSource( loader );
    source->setFunctor( functor );
    source->setup( *this );
    return source;
}

std::ostream& operator << ( std::ostream& os, const URIHandler& params )
{
    switch( params.getType( ))
    {
    case VolumeType::compartments:
        os << "compartment voltages from " << params.getReport();
        break;
    case VolumeType::somas:
        os << "soma voltages from " << params.getReport();
        break;
    case VolumeType::spikes:
        os << "spikes from " << params.getConfig().getSpikeSource()
           << ", duration = " << params.getDuration();
        break;
    case VolumeType::synapses:
        os << "synapse positions from " << params.getConfig().getSynapseSource();
        break;
    case VolumeType::vsd:
        os << "VSD (Voltage-Sensitive Dye) from " << params.getReport();
        break;
    case VolumeType::test:
        os << "test type for validation";
        break;
    case VolumeType::unknown:
    default:
        os << "unknown data source";
        break;
    }

    os << ", using ";
    switch( params.getFunctorType( ))
    {
    case FunctorType::density:
        os << "density functor";
        break;
    case FunctorType::field:
        os << "field functor";
        break;
    case FunctorType::frequency:
        os << "frequency functor";
        break;
    case FunctorType::lfp:
        os << "LFP functor";
        break;
    case FunctorType::unknown:
    default:
        os << "unknown functor";
        break;
    }

    return os << ", input data range = " << params.getInputRange()
              << ", resolution = " << params.getResolution();
}

}

// template instantiations
template fivox::ImageSource< itk::Image< uint8_t, 3 >>::Pointer
    fivox::URIHandler::newImageSource() const;
template fivox::ImageSource< itk::Image< float, 3 >>::Pointer
    fivox::URIHandler::newImageSource() const;
