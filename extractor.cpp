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

#include "Extractor/ExtractorCallbacks.h"
#include "Extractor/ExtractionContainers.h"
#include "Extractor/ScriptingEnvironment.h"
#include "Extractor/PBFParser.h"
#include "Extractor/XMLParser.h"
#include "Util/GitDescription.h"
#include "Util/MachineInfo.h"
#include "Util/OpenMPWrapper.h"
#include "Util/OSRMException.h"
#include "Util/ProgramOptions.h"
#include "Util/SimpleLogger.h"
#include "Util/StringUtil.h"
#include "Util/TimingUtil.h"
#include "Util/UUID.h"
#include "typedefs.h"

#include <cstdlib>

#include <iostream>
#include <fstream>
#include <string>

ExtractorCallbacks * extractCallBacks;
UUID uuid;

int main (int argc, char *argv[]) {
    try {
        LogPolicy::GetInstance().Unmute();
        double startup_time = get_timestamp();

        boost::filesystem::path config_file_path, input_path, profile_path;
        int requested_num_threads;

        // declare a group of options that will be allowed only on command line
        boost::program_options::options_description generic_options("Options");
        generic_options.add_options()
            ("version,v", "Show version")
            ("help,h", "Show this help message")
            ("config,c", boost::program_options::value<boost::filesystem::path>(&config_file_path)->default_value("extractor.ini"),
                  "Path to a configuration file.");

        // declare a group of options that will be allowed both on command line and in config file
        boost::program_options::options_description config_options("Configuration");
        config_options.add_options()
            ("profile,p", boost::program_options::value<boost::filesystem::path>(&profile_path)->default_value("profile.lua"),
                "Path to LUA routing profile")
            ("threads,t", boost::program_options::value<int>(&requested_num_threads)->default_value(8),
                "Number of threads to use");

        // hidden options, will be allowed both on command line and in config file, but will not be shown to the user
        boost::program_options::options_description hidden_options("Hidden options");
        hidden_options.add_options()
            ("input,i", boost::program_options::value<boost::filesystem::path>(&input_path),
                "Input file in .osm, .osm.bz2 or .osm.pbf format");

        // positional option
        boost::program_options::positional_options_description positional_options;
        positional_options.add("input", 1);

        // combine above options for parsing
        boost::program_options::options_description cmdline_options;
        cmdline_options.add(generic_options).add(config_options).add(hidden_options);

        boost::program_options::options_description config_file_options;
        config_file_options.add(config_options).add(hidden_options);

        boost::program_options::options_description visible_options(boost::filesystem::basename(argv[0]) + " <input.osm/.osm.bz2/.osm.pbf> [options]");
        visible_options.add(generic_options).add(config_options);

        // parse command line options
        boost::program_options::variables_map option_variables;
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).
            options(cmdline_options).positional(positional_options).run(), option_variables);

        if(option_variables.count("version")) {
            SimpleLogger().Write() << g_GIT_DESCRIPTION;
            return 0;
        }

        if(option_variables.count("help")) {
            SimpleLogger().Write() << visible_options;
            return 0;
        }

        boost::program_options::notify(option_variables);

        // parse config file
        if(boost::filesystem::is_regular_file(config_file_path)) {
            SimpleLogger().Write() << "Reading options from: " << config_file_path.c_str();
            std::string config_str;
            PrepareConfigFile( config_file_path.c_str(), config_str );
            std::stringstream config_stream( config_str );
            boost::program_options::store(parse_config_file(config_stream, config_file_options), option_variables);
            boost::program_options::notify(option_variables);
        }

        if(!option_variables.count("input")) {
            SimpleLogger().Write(logWARNING) << "No input file specified";
            SimpleLogger().Write() << visible_options;
            return -1;
        }

        if(1 > requested_num_threads) {
            SimpleLogger().Write(logWARNING) << "Number of threads must be 1 or larger";
            return -1;
        }

        SimpleLogger().Write() << "Input file: " << input_path.filename().string();
        SimpleLogger().Write() << "Profile: " << profile_path.filename().string();
        SimpleLogger().Write() << "Threads: " << requested_num_threads;

        /*** Setup Scripting Environment ***/
        ScriptingEnvironment scriptingEnvironment(profile_path.c_str());

        omp_set_num_threads( std::min( omp_get_num_procs(), requested_num_threads) );

        bool file_has_pbf_format(false);
        std::string output_file_name(input_path.c_str());
        std::string restrictionsFileName(input_path.c_str());
        std::string::size_type pos = output_file_name.find(".osm.bz2");
        if(pos==std::string::npos) {
            pos = output_file_name.find(".osm.pbf");
            if(pos!=std::string::npos) {
                file_has_pbf_format = true;
            }
        }
        if(pos==std::string::npos) {
            pos = output_file_name.find(".pbf");
            if(pos!=std::string::npos) {
                file_has_pbf_format = true;
            }
        }
        if(pos!=std::string::npos) {
            output_file_name.replace(pos, 8, ".osrm");
            restrictionsFileName.replace(pos, 8, ".osrm.restrictions");
        } else {
            pos=output_file_name.find(".osm");
            if(pos!=std::string::npos) {
                output_file_name.replace(pos, 5, ".osrm");
                restrictionsFileName.replace(pos, 5, ".osrm.restrictions");
            } else {
                output_file_name.append(".osrm");
                restrictionsFileName.append(".osrm.restrictions");
            }
        }

        StringMap stringMap;
        ExtractionContainers externalMemory;

        stringMap[""] = 0;
        extractCallBacks = new ExtractorCallbacks(&externalMemory, &stringMap);
        BaseParser* parser;
        if(file_has_pbf_format) {
            parser = new PBFParser(input_path.c_str(), extractCallBacks, scriptingEnvironment);
        } else {
            parser = new XMLParser(input_path.c_str(), extractCallBacks, scriptingEnvironment);
        }

        if(!parser->ReadHeader()) {
            throw OSRMException("Parser not initialized!");
        }
        SimpleLogger().Write() << "Parsing in progress..";
        double parsing_start_time = get_timestamp();
        parser->Parse();
        SimpleLogger().Write() << "Parsing finished after " <<
            (get_timestamp() - parsing_start_time) <<
            " seconds";

        if( externalMemory.all_edges_list.empty() ) {
            SimpleLogger().Write(logWARNING) << "The input data is empty, exiting.";
            return -1;
        }

        externalMemory.PrepareData(output_file_name, restrictionsFileName);

        delete parser;
        delete extractCallBacks;

        SimpleLogger().Write() <<
            "extraction finished after " << get_timestamp() - startup_time <<
            "s";

         SimpleLogger().Write() << "To prepare the data for routing, run: "
            << "./osrm-prepare " << output_file_name << std::endl;

    } catch(boost::program_options::too_many_positional_options_error& e) {
        SimpleLogger().Write(logWARNING) << "Only one input file can be specified";
        return -1;
    } catch(boost::program_options::error& e) {
        SimpleLogger().Write(logWARNING) << e.what();
        return -1;
    } catch(std::exception & e) {
        SimpleLogger().Write(logWARNING) << "Unhandled exception: " << e.what();
        return -1;
    }
    return 0;
}
