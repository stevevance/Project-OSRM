/*

Copyright (c) 2013, Project OSRM, Dennis Luxen, others
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "OSRM.h"
#include "OSRM_impl.h"

#include "../Plugins/HelloWorldPlugin.h"
#include "../Plugins/LocatePlugin.h"
#include "../Plugins/NearestPlugin.h"
#include "../Plugins/TimestampPlugin.h"
#include "../Plugins/ViaRoutePlugin.h"

#include "../Server/DataStructures/BaseDataFacade.h"
#include "../Server/DataStructures/InternalDataFacade.h"
#include "../Server/DataStructures/SharedBarriers.h"
#include "../Server/DataStructures/SharedDataFacade.h"

#include <boost/assert.hpp>

OSRM_impl::OSRM_impl( const ServerPaths & server_paths, const bool use_shared_memory )
 :
    use_shared_memory(use_shared_memory)
{
    if (use_shared_memory)
    {
        barrier = new SharedBarriers();
        query_data_facade = new SharedDataFacade<QueryEdge::EdgeData>( );
    }
    else
    {
        query_data_facade = new InternalDataFacade<QueryEdge::EdgeData>(
            server_paths
        );
    }

    //The following plugins handle all requests.
    RegisterPlugin(
        new HelloWorldPlugin()
    );
    RegisterPlugin(
        new LocatePlugin<BaseDataFacade<QueryEdge::EdgeData> >(
            query_data_facade
        )
    );
    RegisterPlugin(
        new NearestPlugin<BaseDataFacade<QueryEdge::EdgeData> >(
            query_data_facade
        )
    );
    RegisterPlugin(
        new TimestampPlugin<BaseDataFacade<QueryEdge::EdgeData> >(
            query_data_facade
        )
    );
    RegisterPlugin(
        new ViaRoutePlugin<BaseDataFacade<QueryEdge::EdgeData> >(
            query_data_facade
        )
    );
}

OSRM_impl::~OSRM_impl() {
    BOOST_FOREACH(PluginMap::value_type & plugin_pointer, plugin_map) {
        delete plugin_pointer.second;
    }
    if( use_shared_memory ) {
        delete barrier;
    }
}

void OSRM_impl::RegisterPlugin(BasePlugin * plugin) {
    SimpleLogger().Write()  << "loaded plugin: " << plugin->GetDescriptor();
    if( plugin_map.find(plugin->GetDescriptor()) != plugin_map.end() ) {
        delete plugin_map.find(plugin->GetDescriptor())->second;
    }
    plugin_map.emplace(plugin->GetDescriptor(), plugin);
}

void OSRM_impl::RunQuery(RouteParameters & route_parameters, http::Reply & reply) {
    const PluginMap::const_iterator & iter = plugin_map.find(
        route_parameters.service
    );

    if(plugin_map.end() != iter) {
        reply.status = http::Reply::ok;
        if( use_shared_memory ) {
            // lock update pending
            boost::interprocess::scoped_lock<
                boost::interprocess::named_mutex
            > pending_lock(barrier->pending_update_mutex);

            // lock query
            boost::interprocess::scoped_lock<
                boost::interprocess::named_mutex
            > query_lock(barrier->query_mutex);

            // unlock update pending
            pending_lock.unlock();

            // increment query count
            ++(barrier->number_of_queries);

            (static_cast<SharedDataFacade<QueryEdge::EdgeData>* >(query_data_facade))->CheckAndReloadFacade();
        }

        iter->second->HandleRequest(route_parameters, reply );
        if( use_shared_memory ) {
            // lock query
            boost::interprocess::scoped_lock<
                boost::interprocess::named_mutex
            > query_lock(barrier->query_mutex);

            // decrement query count
            --(barrier->number_of_queries);
            BOOST_ASSERT_MSG(
                0 <= barrier->number_of_queries,
                "invalid number of queries"
            );

            // notify all processes that were waiting for this condition
            if (0 == barrier->number_of_queries) {
                barrier->no_running_queries_condition.notify_all();
            }
        }
    } else {
        reply = http::Reply::StockReply(http::Reply::badRequest);
    }
}

// proxy code for compilation firewall

OSRM::OSRM(
    const ServerPaths & paths,
    const bool use_shared_memory
) : OSRM_pimpl_(new OSRM_impl(paths, use_shared_memory)) { }

OSRM::~OSRM() {
    delete OSRM_pimpl_;
}

void OSRM::RunQuery(RouteParameters & route_parameters, http::Reply & reply) {
    OSRM_pimpl_->RunQuery(route_parameters, reply);
}
