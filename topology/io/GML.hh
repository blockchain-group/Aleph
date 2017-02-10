#ifndef ALEPH_TOPOLOGY_IO_GML_HH__
#define ALEPH_TOPOLOGY_IO_GML_HH__

#include <fstream>
#include <set>
#include <regex>
#include <stack>
#include <string>
#include <vector>

// FIXME: Remove after debugging
#include <iostream>

#include "utilities/String.hh"

namespace aleph
{

namespace topology
{

namespace io
{

/**
  @class GMLReader
  @brief Parses files in GML (Graph Modeling Language) format

  This is a simple reader for graphs in GML format. It suports a basic
  subset of the GML specification, viz. the specification of different
  attributes for nodes, as well as weight specifications for edges.

  Currently, the following attributes will be read:

  * \c id (for nodes)
  * \c label (for nodes)
  * \c source (for edges)
  * \c target (for edges)
  * \c weight (for edges)
*/

class GMLReader
{
public:

  template <class SimplicialComplex> void operator()( const std::string& filename, SimplicialComplex& K )
  {
    std::ifstream in( filename );
    if( !in )
      throw std::runtime_error( "Unable to read input file" );

    this->operator()( in, K );
  }

  template <class SimplicialComplex> void operator()( std::ifstream& in, SimplicialComplex& K )
  {
    using namespace aleph::utilities;

    using Simplex           = typename SimplicialComplex::ValueType;
    using DataType          = typename Simplex::DataType;
    using VertexType        = typename Simplex::VertexType;

    std::string line;
    std::set<std::string> levels     = { "graph", "node", "edge" };
    std::set<std::string> attributes = { "id", "label", "source", "target", "weight" };

    auto isLevel = [&levels] ( const std::string& name )
    {
      return levels.find( name ) != levels.end();
    };

    auto isAttribute = [&attributes] ( const std::string& name )
    {
      return attributes.find( name ) != attributes.end();
    };

    // Specifies the current level the parse is in. May be either one of
    // the known levels above.
    std::stack<std::string> currentLevel;

    // Last level that was read by the parser. If an open bracket '[' is
    // identified, this will become the current level.
    std::string lastLevel;

    std::vector<Node> nodes;
    std::vector<Edge> edges;

    Graph graph;
    Node node;
    Edge edge;

    std::regex reAttribute = std::regex( "([[:alpha:]]+)[[:space:]]*.*" );
    std::regex reKeyValue  = std::regex( "([[:alpha:]]+)[[:space:]]+([[:alpha:]]+)" );
    std::regex reLabel     = std::regex( "(label)[[:space:]]+\"([^\"]+)\"" );

    while( std::getline( in, line ) )
    {
      line = trim( line );

      // Skip comment lines. This is somewhat wasteful because I am
      // splitting the complete string even though I merely need to
      // know the first token.
      {
        auto tokens = split( line );
        if( tokens.empty() == false && tokens.front() == "comment" )
          continue;
      }

      // Detecting a new level
      if( isLevel( line ) )
      {
        if( lastLevel.empty() )
          lastLevel = line;
        else
          throw std::runtime_error( "Encountered incorrectly-nested levels" );
      }

      // Opening a new level
      else if( line == "[" )
      {
        std::cerr << "* Entering level = " << lastLevel << "\n";
        currentLevel.push( lastLevel );
        lastLevel = "";
      }

      // Closing a new level
      else if( line == "]" )
      {
        std::cerr << "* Leaving level = " << currentLevel.top() << "\n";

        if( currentLevel.top() == "node" )
          nodes.push_back( node );
        else if( currentLevel.top() == "edge" )
          edges.push_back( edge );

        // Reset node and edge data structure to fill them again once
        // a new level is being encountered.
        node = {};
        edge = {};

        currentLevel.pop();
      }

      // Check for attributes
      else
      {
        std::smatch matches;

        auto&& dict = currentLevel.top() == "node" ? node.dict
                                                   : currentLevel.top() == "edge" ? edge.dict
                                                                                  : currentLevel.top() == "graph" ? graph.dict
                                                                                                                  : throw std::runtime_error( "Current level is unknown" );

        if( std::regex_match( line, matches, reAttribute ) )
        {
          auto name = matches[1];
          if( isAttribute( name ) )
          {
            // Special matching for labels
            if( name == "label" )
              std::regex_match( line, matches, reLabel );

            // Regular matching for all other attributes
            else
              std::regex_match( line, matches, reKeyValue );

            auto value = matches[2];

            if( name == "id" )
              node.id = value;
            else if( name == "source" )
              edge.source = value;
            else if( name == "target" )
              edge.target = value;

            // Just add it to the dictionary of optional values
            else
              dict[name] = value;
          }
          // Skip unknown attributes...
          else
          {
          }
        }
      }
    }

    // Creates nodes (vertices) ----------------------------------------

    std::set<std::string> nodeIDs;

    for( auto&& node : nodes )
      nodeIDs.insert( node.id );

    if( nodeIDs.size() != nodes.size() )
      throw std::runtime_error( "Encountered duplicate node ID" );

    // Lambda expression for creating a numerical ID out of a parsed ID.
    // This ensures that internal IDs always start with a zero.
    auto getID = [&nodeIDs] ( const std::string& id )
    {
      return static_cast<VertexType>(
        std::distance( nodeIDs.begin(), nodeIDs.find( id ) )
      );
    };

    std::vector<Simplex> simplices;
    simplices.reserve( nodes.size() + edges.size() );

    for( auto&& node : nodes )
    {
      auto id = getID( node.id );

      if( node.dict.find( "weight" ) != node.dict.end() )
        simplices.push_back( Simplex( id, convert<DataType>( node.dict.at( "weight" ) ) ) );
      else
        simplices.push_back( Simplex( id ) );
    }

    // Create edges ----------------------------------------------------

    auto getSimplexByID = [&simplices] ( VertexType id )
    {
      auto position = std::find_if( simplices.begin(), simplices.end(),
                                    [&id] ( const Simplex& s )
                                    {
                                      return s.dimension() == 0 && s[0] == id;
                                    } );

      if( position != simplices.end() )
        return *position;
      else
        throw std::runtime_error( "Querying unknown simplex for edge creation" );
    };

    for( auto&& edge : edges )
    {
      auto u = getID( edge.source );
      auto v = getID( edge.target );

      // No optional data attached; need to create weight based on node
      // weights, if those are available.
      if( edge.dict.find( "weight" ) == edge.dict.end() )
      {
        auto uSimplex = getSimplexByID( u );
        auto vSimplex = getSimplexByID( v );

        // TODO: Permit the usage of other weight assignment strategies
        // here, for example by using a functor.
        auto data = std::max( uSimplex.data(), vSimplex.data() );

        simplices.push_back( Simplex( {u,v}, data ) );
      }

      // Use converted weight
      else
        simplices.push_back( Simplex( {u,v}, convert<DataType>( edge.dict.at( "weight" ) ) ) );
    }

    K = SimplicialComplex( simplices.begin(), simplices.end() );
  }

private:

  /** Describes a parsed graph along with all of its attributes */
  struct Graph
  {
    std::map<std::string, std::string> dict; // all remaining attributes
  };

  /** Describes a parsed node along with all of its attributes */
  struct Node
  {
    std::string id;

    std::map<std::string, std::string> dict; // all remaining attributes
  };

  /** Describes a parsed edge along with all of its attributes */
  struct Edge
  {
    std::string source;
    std::string target;

    std::map<std::string, std::string> dict; // all remaining attributes
  };

};

} // namespace io

} // namespace topology

} // namespace aleph

// graph
// [
//   node
//   [
//    id A
//   ]
//   node
//   [
//    id B
//   ]
//   node
//   [
//    id C
//   ]
//    edge
//   [
//    source B
//    target A
//   ]
//   edge
//   [
//    source C
//    target A
//   ]
// ]

#endif