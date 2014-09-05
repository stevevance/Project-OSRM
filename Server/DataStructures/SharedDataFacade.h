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

#ifndef SHARED_DATA_FACADE_H
#define SHARED_DATA_FACADE_H

// implements all data storage when shared memory _IS_ used

#include "BaseDataFacade.h"
#include "SharedDataType.h"

#include "../../DataStructures/RangeTable.h"
#include "../../DataStructures/StaticGraph.h"
#include "../../DataStructures/StaticRTree.h"
#include "../../Util/BoostFileSystemFix.h"
#include "../../Util/ProgramOptions.h"
#include "../../Util/SimpleLogger.h"

#include <algorithm>
#include <memory>

template <class EdgeDataT> class SharedDataFacade : public BaseDataFacade<EdgeDataT>
{

  private:
    typedef EdgeDataT EdgeData;
    typedef BaseDataFacade<EdgeData> super;
    typedef StaticGraph<EdgeData, true> QueryGraph;
    typedef typename StaticGraph<EdgeData, true>::NodeArrayEntry GraphNode;
    typedef typename StaticGraph<EdgeData, true>::EdgeArrayEntry GraphEdge;
    typedef typename RangeTable<16, true>::BlockT NameIndexBlock;
    typedef typename QueryGraph::InputEdge InputEdge;
    typedef typename super::RTreeLeaf RTreeLeaf;
    typedef typename StaticRTree<RTreeLeaf, ShM<FixedPointCoordinate, true>::vector, true>::TreeNode
    RTreeNode;

    SharedDataLayout *data_layout;
    char *shared_memory;
    SharedDataTimestamp *data_timestamp_ptr;

    SharedDataType CURRENT_LAYOUT;
    SharedDataType CURRENT_DATA;
    unsigned CURRENT_TIMESTAMP;

    unsigned m_check_sum;
    unsigned m_number_of_nodes;
    std::shared_ptr<QueryGraph> m_query_graph;
    std::shared_ptr<SharedMemory> m_layout_memory;
    std::shared_ptr<SharedMemory> m_large_memory;
    std::string m_timestamp;

    std::shared_ptr<ShM<FixedPointCoordinate, true>::vector> m_coordinate_list;
    ShM<NodeID, true>::vector m_via_node_list;
    ShM<unsigned, true>::vector m_name_ID_list;
    ShM<TurnInstruction, true>::vector m_turn_instruction_list;
    ShM<char, true>::vector m_names_char_list;
    ShM<unsigned, true>::vector m_name_begin_indices;
    ShM<bool, true>::vector m_egde_is_compressed;
    ShM<unsigned, true>::vector m_geometry_indices;
    ShM<unsigned, true>::vector m_geometry_list;

    boost::thread_specific_ptr<StaticRTree<RTreeLeaf, ShM<FixedPointCoordinate, true>::vector, true>>
    m_static_rtree;
    boost::filesystem::path file_index_path;

    std::shared_ptr<RangeTable<16, true>> m_name_table;

    void LoadChecksum()
    {
        m_check_sum =
            *data_layout->GetBlockPtr<unsigned>(shared_memory, SharedDataLayout::HSGR_CHECKSUM);
        SimpleLogger().Write() << "set checksum: " << m_check_sum;
    }

    void LoadTimestamp()
    {
        char *timestamp_ptr =
            data_layout->GetBlockPtr<char>(shared_memory, SharedDataLayout::TIMESTAMP);
        m_timestamp.resize(data_layout->GetBlockSize(SharedDataLayout::TIMESTAMP));
        std::copy(timestamp_ptr,
                  timestamp_ptr + data_layout->GetBlockSize(SharedDataLayout::TIMESTAMP),
                  m_timestamp.begin());
    }

    void LoadRTree()
    {
        BOOST_ASSERT_MSG(!m_coordinate_list->empty(), "coordinates must be loaded before r-tree");

        RTreeNode *tree_ptr =
            data_layout->GetBlockPtr<RTreeNode>(shared_memory, SharedDataLayout::R_SEARCH_TREE);
        m_static_rtree.reset(
            new StaticRTree<RTreeLeaf, ShM<FixedPointCoordinate, true>::vector, true>(
                tree_ptr,
                data_layout->num_entries[SharedDataLayout::R_SEARCH_TREE],
                file_index_path,
                m_coordinate_list)
        );
    }

    void LoadGraph()
    {
        m_number_of_nodes = data_layout->num_entries[SharedDataLayout::GRAPH_NODE_LIST];
        GraphNode *graph_nodes_ptr =
            data_layout->GetBlockPtr<GraphNode>(shared_memory, SharedDataLayout::GRAPH_NODE_LIST);

        GraphEdge *graph_edges_ptr =
            data_layout->GetBlockPtr<GraphEdge>(shared_memory, SharedDataLayout::GRAPH_EDGE_LIST);

        typename ShM<GraphNode, true>::vector node_list(
            graph_nodes_ptr, data_layout->num_entries[SharedDataLayout::GRAPH_NODE_LIST]);
        typename ShM<GraphEdge, true>::vector edge_list(
            graph_edges_ptr, data_layout->num_entries[SharedDataLayout::GRAPH_EDGE_LIST]);
        m_query_graph.reset(new QueryGraph(node_list, edge_list));
    }

    void LoadNodeAndEdgeInformation()
    {

        FixedPointCoordinate *coordinate_list_ptr = data_layout->GetBlockPtr<FixedPointCoordinate>(
            shared_memory, SharedDataLayout::COORDINATE_LIST);
        m_coordinate_list = std::make_shared<ShM<FixedPointCoordinate, true>::vector>(
            coordinate_list_ptr, data_layout->num_entries[SharedDataLayout::COORDINATE_LIST]);

        TurnInstruction *turn_instruction_list_ptr = data_layout->GetBlockPtr<TurnInstruction>(
            shared_memory, SharedDataLayout::TURN_INSTRUCTION);
        typename ShM<TurnInstruction, true>::vector turn_instruction_list(
            turn_instruction_list_ptr,
            data_layout->num_entries[SharedDataLayout::TURN_INSTRUCTION]);
        m_turn_instruction_list.swap(turn_instruction_list);

        unsigned *name_id_list_ptr =
            data_layout->GetBlockPtr<unsigned>(shared_memory, SharedDataLayout::NAME_ID_LIST);
        typename ShM<unsigned, true>::vector name_id_list(
            name_id_list_ptr, data_layout->num_entries[SharedDataLayout::NAME_ID_LIST]);
        m_name_ID_list.swap(name_id_list);
    }

    void LoadViaNodeList()
    {
        NodeID *via_node_list_ptr =
            data_layout->GetBlockPtr<NodeID>(shared_memory, SharedDataLayout::VIA_NODE_LIST);
        typename ShM<NodeID, true>::vector via_node_list(
            via_node_list_ptr, data_layout->num_entries[SharedDataLayout::VIA_NODE_LIST]);
        m_via_node_list.swap(via_node_list);
    }

    void LoadNames()
    {
        unsigned *offsets_ptr =
            data_layout->GetBlockPtr<unsigned>(shared_memory, SharedDataLayout::NAME_OFFSETS);
        NameIndexBlock *blocks_ptr =
            data_layout->GetBlockPtr<NameIndexBlock>(shared_memory, SharedDataLayout::NAME_BLOCKS);
        typename ShM<unsigned, true>::vector name_offsets(
            offsets_ptr, data_layout->num_entries[SharedDataLayout::NAME_OFFSETS]);
        typename ShM<NameIndexBlock, true>::vector name_blocks(
            blocks_ptr, data_layout->num_entries[SharedDataLayout::NAME_BLOCKS]);

        char *names_list_ptr =
            data_layout->GetBlockPtr<char>(shared_memory, SharedDataLayout::NAME_CHAR_LIST);
        typename ShM<char, true>::vector names_char_list(
            names_list_ptr, data_layout->num_entries[SharedDataLayout::NAME_CHAR_LIST]);
        m_name_table = std::make_shared<RangeTable<16, true>>(
            name_offsets, name_blocks, names_char_list.size());

        m_names_char_list.swap(names_char_list);
    }

    void LoadGeometries()
    {
        unsigned *geometries_compressed_ptr = data_layout->GetBlockPtr<unsigned>(
            shared_memory, SharedDataLayout::GEOMETRIES_INDICATORS);
        typename ShM<bool, true>::vector egde_is_compressed(
            geometries_compressed_ptr,
            data_layout->num_entries[SharedDataLayout::GEOMETRIES_INDICATORS]);
        m_egde_is_compressed.swap(egde_is_compressed);

        unsigned *geometries_index_ptr =
            data_layout->GetBlockPtr<unsigned>(shared_memory, SharedDataLayout::GEOMETRIES_INDEX);
        typename ShM<unsigned, true>::vector geometry_begin_indices(
            geometries_index_ptr, data_layout->num_entries[SharedDataLayout::GEOMETRIES_INDEX]);
        m_geometry_indices.swap(geometry_begin_indices);

        unsigned *geometries_list_ptr =
            data_layout->GetBlockPtr<unsigned>(shared_memory, SharedDataLayout::GEOMETRIES_LIST);
        typename ShM<unsigned, true>::vector geometry_list(
            geometries_list_ptr, data_layout->num_entries[SharedDataLayout::GEOMETRIES_LIST]);
        m_geometry_list.swap(geometry_list);
    }

  public:
    virtual ~SharedDataFacade() {}

    SharedDataFacade()
    {
        data_timestamp_ptr = (SharedDataTimestamp *)SharedMemoryFactory::Get(
                                 CURRENT_REGIONS, sizeof(SharedDataTimestamp), false, false)->Ptr();

        CURRENT_LAYOUT = LAYOUT_NONE;
        CURRENT_DATA = DATA_NONE;
        CURRENT_TIMESTAMP = 0;

        // load data
        CheckAndReloadFacade();
    }

    void CheckAndReloadFacade()
    {
        if (CURRENT_LAYOUT != data_timestamp_ptr->layout ||
            CURRENT_DATA != data_timestamp_ptr->data ||
            CURRENT_TIMESTAMP != data_timestamp_ptr->timestamp)
        {
            // release the previous shared memory segments
            SharedMemory::Remove(CURRENT_LAYOUT);
            SharedMemory::Remove(CURRENT_DATA);

            CURRENT_LAYOUT = data_timestamp_ptr->layout;
            CURRENT_DATA = data_timestamp_ptr->data;
            CURRENT_TIMESTAMP = data_timestamp_ptr->timestamp;

            m_layout_memory.reset(SharedMemoryFactory::Get(CURRENT_LAYOUT));

            data_layout = (SharedDataLayout *)(m_layout_memory->Ptr());

            m_large_memory.reset(SharedMemoryFactory::Get(CURRENT_DATA));
            shared_memory = (char *)(m_large_memory->Ptr());

            const char *file_index_ptr =
                data_layout->GetBlockPtr<char>(shared_memory, SharedDataLayout::FILE_INDEX_PATH);
            file_index_path = boost::filesystem::path(file_index_ptr);
            if (!boost::filesystem::exists(file_index_path))
            {
                SimpleLogger().Write(logDEBUG) << "Leaf file name " << file_index_path.string();
                throw OSRMException("Could not load leaf index file."
                                    "Is any data loaded into shared memory?");
            }

            LoadGraph();
            LoadChecksum();
            LoadNodeAndEdgeInformation();
            LoadGeometries();
            LoadTimestamp();
            LoadViaNodeList();
            LoadNames();

            data_layout->PrintInformation();

            SimpleLogger().Write() << "number of geometries: " << m_coordinate_list->size();
            for (unsigned i = 0; i < m_coordinate_list->size(); ++i)
            {
                if(!GetCoordinateOfNode(i).isValid())
                {
                    SimpleLogger().Write() << "coordinate " << i << " not valid";
                }
            }
        }
    }

    // search graph access
    unsigned GetNumberOfNodes() const { return m_query_graph->GetNumberOfNodes(); }

    unsigned GetNumberOfEdges() const { return m_query_graph->GetNumberOfEdges(); }

    unsigned GetOutDegree(const NodeID n) const { return m_query_graph->GetOutDegree(n); }

    NodeID GetTarget(const EdgeID e) const { return m_query_graph->GetTarget(e); }

    EdgeDataT &GetEdgeData(const EdgeID e) { return m_query_graph->GetEdgeData(e); }

    // const EdgeDataT &GetEdgeData( const EdgeID e ) const {
    //     return m_query_graph->GetEdgeData(e);
    // }

    EdgeID BeginEdges(const NodeID n) const { return m_query_graph->BeginEdges(n); }

    EdgeID EndEdges(const NodeID n) const { return m_query_graph->EndEdges(n); }

    EdgeRange GetAdjacentEdgeRange(const NodeID node) const
    {
        return m_query_graph->GetAdjacentEdgeRange(node);
    };

    // searches for a specific edge
    EdgeID FindEdge(const NodeID from, const NodeID to) const
    {
        return m_query_graph->FindEdge(from, to);
    }

    EdgeID FindEdgeInEitherDirection(const NodeID from, const NodeID to) const
    {
        return m_query_graph->FindEdgeInEitherDirection(from, to);
    }

    EdgeID FindEdgeIndicateIfReverse(const NodeID from, const NodeID to, bool &result) const
    {
        return m_query_graph->FindEdgeIndicateIfReverse(from, to, result);
    }

    // node and edge information access
    FixedPointCoordinate GetCoordinateOfNode(const NodeID id) const
    {
        return m_coordinate_list->at(id);
    };

    virtual bool EdgeIsCompressed(const unsigned id) const { return m_egde_is_compressed.at(id); }

    virtual void GetUncompressedGeometry(const unsigned id, std::vector<unsigned> &result_nodes)
        const
    {
        const unsigned begin = m_geometry_indices.at(id);
        const unsigned end = m_geometry_indices.at(id + 1);

        result_nodes.clear();
        result_nodes.insert(
            result_nodes.begin(), m_geometry_list.begin() + begin, m_geometry_list.begin() + end);
    }

    virtual unsigned GetGeometryIndexForEdgeID(const unsigned id) const
    {
        return m_via_node_list.at(id);
    }

    TurnInstruction GetTurnInstructionForEdgeID(const unsigned id) const
    {
        return m_turn_instruction_list.at(id);
    }

    bool LocateClosestEndPointForCoordinate(const FixedPointCoordinate &input_coordinate,
                                            FixedPointCoordinate &result,
                                            const unsigned zoom_level = 18)
    {
        if (!m_static_rtree.get())
        {
            LoadRTree();
        }

        return m_static_rtree->LocateClosestEndPointForCoordinate(
            input_coordinate, result, zoom_level);
    }

    bool FindPhantomNodeForCoordinate(const FixedPointCoordinate &input_coordinate,
                                      PhantomNode &resulting_phantom_node,
                                      const unsigned zoom_level)
    {
        if (!m_static_rtree.get())
        {
            LoadRTree();
        }

        return m_static_rtree->FindPhantomNodeForCoordinate(
            input_coordinate, resulting_phantom_node, zoom_level);
    }

    bool
    IncrementalFindPhantomNodeForCoordinate(const FixedPointCoordinate &input_coordinate,
                                            std::vector<PhantomNode> &resulting_phantom_node_vector,
                                            const unsigned zoom_level,
                                            const unsigned number_of_results)
    {
        if (!m_static_rtree.get())
        {
            LoadRTree();
        }

        return m_static_rtree->IncrementalFindPhantomNodeForCoordinate(
            input_coordinate, resulting_phantom_node_vector, zoom_level, number_of_results);
    }

    unsigned GetCheckSum() const { return m_check_sum; }

    unsigned GetNameIndexFromEdgeID(const unsigned id) const
    {
        return m_name_ID_list.at(id);
    };

    void GetName(const unsigned name_id, std::string &result) const
    {
        if (UINT_MAX == name_id)
        {
            result = "";
            return;
        }
        auto range = m_name_table->GetRange(name_id);

        result.clear();
        if (range.begin() != range.end())
        {
            result.resize(range.back() - range.front() + 1);
            std::copy(m_names_char_list.begin() + range.front(),
                      m_names_char_list.begin() + range.back() + 1,
                      result.begin());
        }
    }

    std::string GetTimestamp() const { return m_timestamp; }
};

#endif // SHARED_DATA_FACADE_H
