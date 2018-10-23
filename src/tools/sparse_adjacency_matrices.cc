/**
  @file  sparse_adjacency_matrices.cc
  @brief Tool for calculating persistent homology of sparse adjacency matrices

  This is a tool shipped by 'Aleph - A Library for Exploring Persistent
  Homology'. It calculates the persistent homology of sparse adjacency
  matrices, i.e. data sets containing *multiple* graphs, using either
  a degree filtration or a filtration based on the *sum* of degrees.

  Original author: Bastian Rieck
*/

#include <aleph/geometry/RipsExpander.hh>

#include <aleph/math/KahanSummation.hh>

#include <aleph/persistentHomology/Calculation.hh>

#include <aleph/topology/FloydWarshall.hh>
#include <aleph/topology/Simplex.hh>
#include <aleph/topology/SimplicialComplex.hh>

#include <aleph/topology/filtrations/Data.hh>
#include <aleph/topology/filtrations/Degree.hh>

#include <aleph/topology/io/GML.hh>
#include <aleph/topology/io/SparseAdjacencyMatrix.hh>

#include <aleph/utilities/Format.hh>
#include <aleph/utilities/String.hh>

#include <getopt.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <cmath>

using DataType          = float;
using VertexType        = std::size_t;
using Simplex           = aleph::topology::Simplex<DataType, VertexType>;
using SimplicialComplex = aleph::topology::SimplicialComplex<Simplex>;

std::vector<DataType> closenessCentrality( const SimplicialComplex& K )
{
  auto M = aleph::topology::floydWarshall( K, 1 );
  auto n = M.numRows();

  std::vector<DataType> result;
  result.reserve( n );

  for( decltype(n) i = 0; i < n; i++ )
  {
    aleph::math::KahanSummation<DataType> sum = DataType();

    for( decltype(n) j = 0; j < n; j++ )
      if( std::isfinite( M(i,j) ) )
        sum += M(i,j);

    result.push_back( DataType(n) / sum );
  }

  return result;
}

void usage()
{
  std::cerr << "Usage: sparse_adjacency_matrices FILE\n"
            << "\n"
            << "Loads a set of sparse adjacency matrices from FILE and performs\n"
            << "several operations with them. By default, the tool will extract\n"
            << "all graphs from the file, use a degree-based filtration, and do\n"
            << "a conversion to GML. Furthermore, persistence diagrams of every\n"
            << "graph will be calculated.\n"
            << "\n"
            << "Optional arguments:\n"
            << "\n"
            << " --dimension D: Expand simplicial complexes up to dimension D\n"
            << " --infinity I:  Use factor I for unpaired points in a diagram\n"
            << "\n"
            << "Flags:\n"
            << "\n"
            << " --closeness-centrality: Calculates closeness centrality filtration\n"
            << " --graphs:               Stores converted graphs in GML format\n"
            << " --sum:                  Calculates degree sum filtration\n"
            << "\n"
            << "\n";
}

int main( int argc, char** argv )
{
  static option commandLineOptions[] =
  {
    { "dimension"           , required_argument, nullptr, 'd' },
    { "infinity"            , required_argument, nullptr, 'f' },
    { "output"              , required_argument, nullptr, 'o' },
    { "closeness-centrality", no_argument      , nullptr, 'c' },
    { "graphs"              , no_argument      , nullptr, 'g' },
    { "sum"                 , no_argument      , nullptr, 's' },
    { nullptr               , 0                , nullptr,  0  }
  };

  unsigned dimension                = 0;
  bool calculateClosenessCentrality = false;
  bool storeGraphs                  = false;
  bool useSumOfDegrees              = false;
  DataType infinity                 = DataType(2);
  std::string output                = "/tmp";

  {
    int option = 0;
    while( ( option = getopt_long( argc, argv, "d:f:o:cgs", commandLineOptions, nullptr ) ) != -1 )
    {
      switch( option )
      {
      case 'd':
        dimension = static_cast<unsigned>( std::stoul( optarg  ) );
        break;
      case 'f':
        infinity = aleph::utilities::convert<DataType>( optarg );
        break;
      case 'o':
        output = optarg;
        break;
      case 'c':
        calculateClosenessCentrality = true;
        break;
      case 'g':
        storeGraphs = true;
        break;
      case 's':
        useSumOfDegrees = true;
        break;
      default:
        throw std::runtime_error( "Unknown command-line arugment" );
      }
    }
  }

  if( ( argc - optind ) < 1 )
  {
    usage();
    return -1;
  }

  // Check that the output parameter at least *looks* like a directory
  if( output.back() != '/' )
    output.push_back( '/' );

  std::string filename = argv[optind++];

  std::vector<SimplicialComplex> simplicialComplexes;
  std::vector<std::string> labels;

  aleph::topology::io::SparseAdjacencyMatrixReader reader;
  reader.setReadGraphLabels();
  reader.setReadNodeLabels();

  std::vector<std::string> nodeLabels;

  std::cerr << "* Reading '" << filename << "'...";

  reader( filename, simplicialComplexes );

  // Get node labels for further processing because we must not drop
  // this valuable information.
  reader.nodeLabels(
    std::back_inserter( nodeLabels )
  );

  std::cerr << "finished\n"
            << "* Read " << simplicialComplexes.size() << " simplicial complexes\n";

  // Calculate closeness centrality ------------------------------------

  if( calculateClosenessCentrality )
  {
    std::size_t index = 0;

    for( auto&& K : simplicialComplexes )
    {
      K.sort();
      auto cc         = closenessCentrality( K );
      auto outputPath = output
                      + aleph::utilities::format( index, simplicialComplexes.size() )
                      + "_closeness_centrality.txt";

      std::cerr << "* Storing closeness centrality values in '" << outputPath << "'\n";

      std::ofstream out( outputPath );
      for( auto&& value : cc )
        out << value << "\n";

      ++index;
    }
  }

  // Expand simplicial complexes ---------------------------------------

  aleph::geometry::RipsExpander<SimplicialComplex> expander;

  if( dimension != 0 )
  {
    std::cerr << "* Expanding simplicial complexes to dimension " << dimension << "...";

    for( auto&& K : simplicialComplexes )
      expander( K, dimension );

    std::cerr << "finished\n";
  }

  // Calculate degrees -------------------------------------------------

  DataType maxDegree = 0;

  std::cerr << "* Calculating degree-based filtration...";

  for( auto&& K : simplicialComplexes )
  {
    std::vector<unsigned> degrees_;
    aleph::topology::filtrations::degrees( K, std::back_inserter( degrees_ ) );

    std::vector<DataType> degrees( degrees_.begin(), degrees_.end() );

    if( !degrees.empty() )
    {
      maxDegree
        = std::max( maxDegree,
                    *std::max_element( degrees.begin(), degrees.end() ) );
    }

    if( useSumOfDegrees )
      K = expander.assignData( K, degrees.begin(), degrees.end(), DataType(0), [] ( DataType a, DataType b ) { return a+b; } );
    else
      K = expander.assignMaximumData( K, degrees.begin(), degrees.end() );

    K.sort( aleph::topology::filtrations::Data<Simplex>() );
  }

  std::cerr << "finished\n"
            << "* Identified maximum degree as D=" << maxDegree << "\n";

  // Store graphs ------------------------------------------------------

  if( storeGraphs )
  {
    aleph::topology::io::GMLWriter writer;
    writer.setNodeLabels( nodeLabels.begin(), nodeLabels.end() );

    for( std::size_t i = 0; i < simplicialComplexes.size(); i++ )
    {
      auto filename = output
                      + aleph::utilities::format( i, simplicialComplexes.size() )
                      + ".gml";

      std::cerr << "* Storing graph in '" << filename << "'...";

      writer( filename, simplicialComplexes[i] );

      std::cerr << "finished\n";
    }
  }

  // Calculate persistent homology -------------------------------------

  {
    std::size_t index = 0;

    for( auto&& K : simplicialComplexes )
    {
      bool dualize                    = true;
      bool includeAllUnpairedCreators = true;

      auto diagrams
        = aleph::calculatePersistenceDiagrams( K,
                                               dualize,
                                               includeAllUnpairedCreators );

      for( auto&& diagram : diagrams )
      {
        diagram.removeDiagonal();

        auto outputPath = output
                        + aleph::utilities::format( index, simplicialComplexes.size() )
                        + "_d"
                        + std::to_string( diagram.dimension() )
                        + ".txt";

        std::ofstream out( outputPath );

        for( auto&& point : diagram )
        {
          if( point.isUnpaired() )
            out << point.x() << "\t" << infinity * maxDegree << "\n";
          else
            out << point.x() << "\t" << point.y() << "\n";
        }
      }

      ++index;
    }
  }

  // Store labels ------------------------------------------------------

  {
    std::vector<std::string> labels;
    reader.graphLabels( std::back_inserter( labels ) );

    auto outputPath = output
                    + "Labels.txt";

    std::cerr << "* Storing labels in '" << outputPath << "'\n";

    std::ofstream out( outputPath );
    for( auto&& label : labels )
      out << label << "\n";
  }
}
