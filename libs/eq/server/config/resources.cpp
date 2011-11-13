
/* Copyright (c) 2011, Stefan Eilemann <eile@eyescale.h> 
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
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

#include "resources.h"

#include "../compound.h"
#include "../frame.h"
#include "../layout.h"
#include "../node.h"
#include "../pipe.h"
#include "../segment.h"
#include "../window.h"
#include "../equalizers/loadEqualizer.h"

#include <eq/client/frame.h>
#include <eq/client/windowSystem.h>
#include <eq/fabric/gpuInfo.h>

#include <gpusd/gpuInfo.h>
#ifdef AGL
#  include <gpusd/cgl/module.h>
#endif
#ifdef GLX
#  include <gpusd/glx/module.h>
#endif
#ifdef EQ_USE_GPUSD_DNSSD
#  include <gpusd/dns_sd/module.h>
#endif

namespace eq
{
namespace server
{
namespace config
{
static co::base::a_int32_t _frameCounter;

#ifdef WGL // TODO remove once gpu-sd has WGL support
typedef eq::fabric::GPUInfo GPUInfo;
typedef eq::fabric::GPUInfos GPUInfos;
typedef eq::fabric::GPUInfosCIter GPUInfosCIter;
#else
typedef gpusd::GPUInfo GPUInfo;
typedef gpusd::GPUInfos GPUInfos;
typedef gpusd::GPUInfosCIter GPUInfosCIter;
#endif


bool Resources::discover( Config* config, const std::string& session )
{
#ifdef WGL // not yet in gpu_sd
    const GPUInfos& infos = WindowSystem().discoverGPUs();
#else
#  ifdef AGL
    gpusd::cgl::Module::use();
#  elif defined (GLX)
    gpusd::glx::Module::use();
#  endif
#  ifdef EQ_USE_GPUSD_DNSSD
    gpusd::dns_sd::Module::use();
#  endif

    const GPUInfos& infos = gpusd::Module::discoverGPUs(
        gpusd::SessionFilter( session ) | gpusd::MirrorFilter( ));
#endif

    if( infos.empty( ))
        return false;

    typedef stde::hash_map< std::string, Node* > NodeMap;

    NodeMap nodes;
    Node* node = new Node( config ); // Add default appNode
    node->setApplicationNode( true );
    nodes[ "" ] = node;

    size_t gpuCounter = 0;
    for( GPUInfosCIter i = infos.begin(); i != infos.end(); ++i )
    {
        const GPUInfo& info = *i;

        node = nodes[ info.hostname ];
        if( !node )
        {
            node = new Node( config );
            nodes[ info.hostname ] = node;
        }

        Pipe* pipe = new Pipe( node );
        pipe->setPort( info.port );
        pipe->setDevice( info.device );
        pipe->setPixelViewport( PixelViewport( info.pvp ));

        std::stringstream name;
        if( info.device == EQ_UNDEFINED_UINT32 )
            name << "display";
        else
            name << "GPU" << ++gpuCounter;

        pipe->setName( name.str( ));
    }
    return true;
}

Channels Resources::configureSourceChannels( Config* config )
{
    Channels channels;

    const Node* node = config->findAppNode();
    EQASSERT( node );
    if( !node )
        return channels;

    const Pipes& pipes = node->getPipes();
    EQASSERT( !pipes.empty( ));
    if( pipes.empty( ))
        return channels;

    Pipe* pipe = pipes.front();
    PixelViewport pvp = pipe->getPixelViewport();
    if( pvp.isValid( ))
    {
        pvp.x = 0;
        pvp.y = 0;
    }
    else
        pvp = PixelViewport( 0, 0, 1920, 1200 );

    if( pipe->getName() != "display" ) // add as resource
        channels.push_back( pipe->getChannel( ChannelPath( 0 )));

    for( PipesCIter i = ++pipes.begin(); i != pipes.end(); ++i )
    {
        pipe = *i;
        Window* window = new Window( pipe );
        window->setPixelViewport( pvp );
        window->setIAttribute( Window::IATTR_HINT_DRAWABLE, fabric::FBO );
        window->setName( pipe->getName() + " source window" );

        channels.push_back( new Channel( window ));
        channels.back()->setName( pipe->getName() + " source channel" );
    }

    return channels;
}

void Resources::configure( const Compounds& compounds, const Channels& channels)
{
    EQASSERT( !compounds.empty( ));
    if( compounds.empty() || channels.empty()) // No additional resources
        return;

    const Canvas* canvas = 0;
    for( CompoundsCIter i = compounds.begin(); i != compounds.end(); ++i )
    {
        const Compounds& children = (*i)->getChildren();
        EQASSERT( children.size() == 1 );
        if( children.size() != 1 )
            continue;

        Compound* segmentCompound = children.front();
        const Channel* channel = segmentCompound->getChannel();
        EQASSERT( channel );

        EQASSERT( !canvas || channel->getCanvas() == canvas );
        canvas = channel->getCanvas();

        const Layout* layout = channel->getLayout();
        EQASSERT( layout );
        
        const std::string& name = layout->getName();
        if( name == "2D" )
        {
            Compound* mono = _add2DCompound( segmentCompound, channels );
            mono->setEyes( EYE_CYCLOP );

            Compound* stereo =_addEyeCompound( segmentCompound, channels );
            stereo->setEyes( EYE_LEFT | EYE_RIGHT );
        }
        else if( name == "static DB" || name == "dynamic DB" )
        {
            Compound* db = _addDBCompound( segmentCompound, channels );
            db->setName( name );
            if( name == "dynamic DB" )
                db->addEqualizer( new LoadEqualizer( LoadEqualizer::MODE_DB ));
        }
        else if( name == "Simple" )
            /* nop */ ;
        else
        {
            EQASSERTINFO( 0, "Unimplemented" );
        }
    }
}

Compound* Resources::_add2DCompound( Compound* root, const Channels& channels )
{
    Compound* compound = new Compound( root );
    compound->setName( "2D" );
    compound->addEqualizer( new LoadEqualizer );
    _addSources( compound, channels );
    return compound;
}

Compound* Resources::_addDBCompound( Compound* root, const Channels& channels )
{
    Compound* compound = new Compound( root );
    compound->setName( "DB" );
    if( channels.size() > 1 )
        compound->setBuffers( eq::Frame::BUFFER_COLOR|eq::Frame::BUFFER_DEPTH );
    _addSources( compound, channels );

    const Compounds& children = compound->getChildren();\
    const size_t step = size_t( 100000.0f / float( children.size( )));
    size_t start = 0;
    for( CompoundsCIter i = children.begin(); i != children.end(); ++i )
    {
        Compound* child = *i;
        if( i+1 == children.end( )) // last - correct rounding 'error'
            child->setRange( Range( float( start ) / 100000.f, 1.f ));
        else
            child->setRange( Range( float( start ) / 100000.f,
                                    float( start + step ) / 100000.f ));
        start += step;
    }
    return compound;
}

void Resources::_addSources( Compound* compound, const Channels& channels )
{
    const Channel* rootChannel = compound->getChannel();
    const Segment* segment = rootChannel->getSegment();
    const Channel* outputChannel = segment ? segment->getChannel() : 0;
    EQASSERT( outputChannel );

    for( ChannelsCIter i = channels.begin(); i != channels.end(); ++i )
    {
        Channel* channel = *i;
        Compound* child = new Compound( compound );

        if( channel == outputChannel )
            continue;
        child->setChannel( channel );

        Frame* outFrame = new Frame;
        std::stringstream frameName;
        frameName << "Frame." << compound->getName() << '.' << ++_frameCounter;
        outFrame->setName( frameName.str( ));
        child->addOutputFrame( outFrame );

        Frame* inFrame = new Frame;
        inFrame->setName( frameName.str( ));
        compound->addInputFrame( inFrame );
    }
}

Compound* Resources::_addEyeCompound( Compound* root, const Channels& channels )
{
    Compound* compound = new Compound( root );
    compound->setName( "Stereo" );

    const size_t nChannels = channels.size();
    const ChannelsCIter split = channels.begin() + (nChannels >> 1);

    Channels leftChannels( split - channels.begin( ));
    std::copy( channels.begin(), split, leftChannels.begin( ));

    Channels rightChannels( channels.end() - (split+1));
    std::copy( split+1, channels.end(), rightChannels.begin( ));
    
    const Channel* rootChannel = compound->getChannel();

    Compound* left = 0;
    if( leftChannels.empty() ||
        ( leftChannels.size() == 1 && leftChannels.front() == rootChannel ))
    {
        left = new Compound( compound );
    }
    else
        left = _add2DCompound( compound, leftChannels );

    left->setEyes( EYE_LEFT | EYE_CYCLOP );

    Compound* right = 0;
    if( rightChannels.empty() ||
        ( rightChannels.size() == 1 && rightChannels.front() == rootChannel ))
    {
        right = new Compound( compound );
    }
    else
        right = _add2DCompound( compound, rightChannels );

    right->setEyes( EYE_RIGHT | EYE_CYCLOP );

    return compound;
}

}
}
}
