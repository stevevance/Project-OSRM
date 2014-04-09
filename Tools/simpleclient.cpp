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

#include "../Library/OSRM.h"
#include "../Util/GitDescription.h"
#include "../Util/ProgramOptions.h"
#include "../Util/SimpleLogger.h"

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <iostream>
#include <stack>
#include <string>
#include <sstream>

//Dude, real recursions on the OS stack? You must be brave...
void print_tree(boost::property_tree::ptree const& property_tree, const unsigned recursion_depth)
{
    boost::property_tree::ptree::const_iterator end = property_tree.end();
    for (boost::property_tree::ptree::const_iterator tree_iterator = property_tree.begin(); tree_iterator != end; ++tree_iterator)
    {
        for (unsigned current_recursion = 0; current_recursion < recursion_depth; ++current_recursion)
        {
            std::cout << " " << std::flush;
        }
        std::cout << tree_iterator->first << ": " << tree_iterator->second.get_value<std::string>() << std::endl;
        print_tree(tree_iterator->second, recursion_depth+1);
    }
}


int main (int argc, const char * argv[]) {
    LogPolicy::GetInstance().Unmute();
    try {
        std::string ip_address;
        int ip_port, requested_thread_num;
        bool use_shared_memory = false, trial = false;
        ServerPaths server_paths;
        if( !GenerateServerProgramOptions(
                argc,
                argv,
                server_paths,
                ip_address,
                ip_port,
                requested_thread_num,
                use_shared_memory,
                trial
             )
        ) {
            return 0;
        }

        SimpleLogger().Write() <<
            "starting up engines, " << g_GIT_DESCRIPTION << ", " <<
            "compiled at " << __DATE__ << ", " __TIME__;

        OSRM routing_machine( server_paths, use_shared_memory );

        RouteParameters route_parameters;
        route_parameters.zoomLevel = 18; //no generalization
        route_parameters.printInstructions = true; //turn by turn instructions
        route_parameters.alternateRoute = true; //get an alternate route, too
        route_parameters.geometry = true; //retrieve geometry of route
        route_parameters.compression = true; //polyline encoding
        route_parameters.checkSum = UINT_MAX; //see wiki
        route_parameters.service = "viaroute"; //that's routing
        route_parameters.outputFormat = "json";
        route_parameters.jsonpParameter = ""; //set for jsonp wrapping
        route_parameters.language = ""; //unused atm
        //route_parameters.hints.push_back(); // see wiki, saves I/O if done properly

        FixedPointCoordinate start_coordinate(52.519930*COORDINATE_PRECISION,13.438640*COORDINATE_PRECISION);
        FixedPointCoordinate target_coordinate(52.513191*COORDINATE_PRECISION,13.415852*COORDINATE_PRECISION);
        route_parameters.coordinates.push_back(start_coordinate);
        route_parameters.coordinates.push_back(target_coordinate);

        http::Reply osrm_reply;

        routing_machine.RunQuery(route_parameters, osrm_reply);

        //attention: super-inefficient hack below:

        std::stringstream my_stream;
        BOOST_FOREACH(const std::string & line, osrm_reply.content)
        {
            std::cout << line;
            my_stream << line;
        }
        std::cout << std::endl;

        boost::property_tree::ptree property_tree;
        boost::property_tree::read_json(my_stream, property_tree);

        print_tree(property_tree, 0);
    } catch (std::exception & current_exception) {
        SimpleLogger().Write(logWARNING) << "caught exception: " << current_exception.what();
        return -1;
    }
    return 0;
}
