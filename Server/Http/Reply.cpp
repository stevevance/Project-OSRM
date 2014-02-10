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

#include <boost/foreach.hpp>

#include <osrm/Reply.h>

#include "../../Util/StringUtil.h"

namespace http {

void Reply::setSize(const unsigned size) {
    BOOST_FOREACH ( Header& h,  headers) {
        if("Content-Length" == h.name) {
            std::string sizeString;
            intToString(size,h.value);
        }
    }
}

void Reply::setSize() {
    unsigned content_length = 0;
    BOOST_FOREACH(const std::string & snippet, content) {
        content_length += snippet.length();
    }
    setSize(content_length);
}

std::vector<boost::asio::const_buffer> Reply::toBuffers(){
    std::vector<boost::asio::const_buffer> buffers;
    buffers.push_back(ToBuffer(status));
    BOOST_FOREACH(const Header & h, headers) {
        buffers.push_back(boost::asio::buffer(h.name));
        buffers.push_back(boost::asio::buffer(seperators));
        buffers.push_back(boost::asio::buffer(h.value));
        buffers.push_back(boost::asio::buffer(crlf));
    }
    buffers.push_back(boost::asio::buffer(crlf));
    BOOST_FOREACH(const std::string & line, content) {
        buffers.push_back(boost::asio::buffer(line));
    }
    return buffers;
}

std::vector<boost::asio::const_buffer> Reply::HeaderstoBuffers(){
    std::vector<boost::asio::const_buffer> buffers;
    buffers.push_back(ToBuffer(status));
    for (std::size_t i = 0; i < headers.size(); ++i) {
        Header& h = headers[i];
        buffers.push_back(boost::asio::buffer(h.name));
        buffers.push_back(boost::asio::buffer(seperators));
        buffers.push_back(boost::asio::buffer(h.value));
        buffers.push_back(boost::asio::buffer(crlf));
    }
    buffers.push_back(boost::asio::buffer(crlf));
    return buffers;
}

Reply Reply::StockReply(Reply::status_type status) {
    Reply rep;
    rep.status = status;
    rep.content.clear();
    rep.content.push_back( ToString(status) );
    rep.headers.resize(3);
    rep.headers[0].name = "Access-Control-Allow-Origin";
    rep.headers[0].value = "*";
    rep.headers[1].name = "Content-Length";

    std::string s;
    intToString(rep.content.size(), s);

    rep.headers[1].value = s;
    rep.headers[2].name = "Content-Type";
    rep.headers[2].value = "text/html";
    return rep;
}

Reply Reply::JsReply(Reply::status_type status, bool isJsonpRequest, std::string filename) {
    Reply rep  = Reply::StockReply(status);
    
    rep.headers.resize(4);
    rep.headers[2].name = "Content-Type";
    rep.headers[2].value = isJsonpRequest ? "text/javascript" : "application/x-javascript";
    rep.headers[3].name = "Content-Disposition";
    rep.headers[3].value = isJsonpRequest ? "attachment; filename=\"" + filename + ".js\"" : "attachment; filename=\"" + filename + ".json\"";
    return rep;
}

std::string Reply::ToString(Reply::status_type status) {
    switch (status) {
    case Reply::ok:
        return okHTML;
    case Reply::badRequest:
        return badRequestHTML;
    default:
        return internalServerErrorHTML;
    }
}

boost::asio::const_buffer Reply::ToBuffer(Reply::status_type status) {
    switch (status) {
    case Reply::ok:
        return boost::asio::buffer(okString);
    case Reply::internalServerError:
        return boost::asio::buffer(internalServerErrorString);
    default:
        return boost::asio::buffer(badRequestString);
    }
}


Reply::Reply() : status(ok) {

}

}
