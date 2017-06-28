/*
  This is a tool shipped by 'Aleph - A Library for Exploring Persistent
  Homology'.

  It reads networks (graphs) in different file formats and approximates
  their topology using a Vietoris--Rips complex. The tool needs all the
  networks to contain weights, which are used to filtrate the complex.

  Vietoris--Rips expansion and the usage of the weights is configurable
  to some extent and may change the output. This tool produces a set of
  persistence diagrams as output and stores them in "/tmp".
*/

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <cassert>
#include <cmath>

// TODO: Replace this as soon as possible with a more modern option
// parser interface.
#include <getopt.h>

#include <aleph/geometry/RipsExpander.hh>

#include <aleph/persistenceDiagrams/Norms.hh>
#include <aleph/persistenceDiagrams/PersistenceDiagram.hh>

#include <aleph/persistentHomology/Calculation.hh>
#include <aleph/persistentHomology/ConnectedComponents.hh>

#include <aleph/topology/Simplex.hh>
#include <aleph/topology/SimplicialComplex.hh>

#include <aleph/topology/filtrations/Data.hh>

#include <aleph/topology/io/EdgeLists.hh>
#include <aleph/topology/io/GML.hh>
#include <aleph/topology/io/Pajek.hh>

#include <aleph/utilities/Filesystem.hh>
#include <aleph/utilities/Format.hh>

using DataType           = double;
using VertexType         = unsigned;
using Simplex            = aleph::topology::Simplex<DataType, VertexType>;
using SimplicialComplex  = aleph::topology::SimplicialComplex<Simplex>;
using PersistenceDiagram = aleph::PersistenceDiagram<DataType>;

std::string formatLabel( const std::string label )
{
  // No whitespace---nothing to do
  if( label.find( '\t' ) == std::string::npos && label.find( ' ' ) == std::string::npos )
    return label;
  else
    return "\""+label+"\"";
}

void usage()
{
  // TODO: Not yet implemented...
}

int main( int argc, char** argv )
{
  static option commandLineOptions[] =
  {
    { "invert-weights", no_argument      , nullptr, 'i' },
    { "normalize"     , no_argument      , nullptr, 'n' },
    { nullptr         , 0                , nullptr,  0  }
  };

  bool invertWeights       = false;
  bool normalize           = false;

  int option = 0;
  while( ( option = getopt_long( argc, argv, "in", commandLineOptions, nullptr ) ) != -1 )
  {
    switch( option )
    {
    case 'i':
      invertWeights = true;
      break;

    case 'n':
      normalize = true;
      break;

    default:
      break;
    }
  }

  if( (argc - optind ) < 2 )
  {
    usage();
    return -1;
  }

  std::string filename = argv[optind++];
  unsigned maxK        = static_cast<unsigned>( std::stoul( argv[optind++] ) );

  SimplicialComplex K;

  // Input -------------------------------------------------------------

  std::cerr << "* Reading '" << filename << "'...";

  if( aleph::utilities::extension( filename ) == ".gml" )
  {
    aleph::topology::io::GMLReader reader;
    reader( filename, K );
  }
  else if( aleph::utilities::extension( filename ) == ".net" )
  {
    aleph::topology::io::PajekReader reader;
    reader( filename, K );
  }
  else
  {
    aleph::topology::io::EdgeListReader reader;
    reader.setReadWeights( true );
    reader.setTrimLines( true );

    reader( filename, K );
  }

  std::cerr << "finished\n";

  // Pre-processing ----------------------------------------------------

  DataType maxWeight = std::numeric_limits<DataType>::lowest();
  DataType minWeight = std::numeric_limits<DataType>::max();
  for( auto&& simplex : K )
  {
    maxWeight = std::max( maxWeight, simplex.data() );
    minWeight = std::min( minWeight, simplex.data() );
  }

  if( normalize && maxWeight != minWeight )
  {
    std::cerr << "* Normalizing weights to [0,1]...";

    auto range = maxWeight - minWeight;

    for (auto it = K.begin(); it != K.end(); ++it )
    {
      if( it->dimension() == 0 )
        continue;

      auto s = *it;

      s.setData( ( s.data() - minWeight ) / range );
      K.replace( it, s );
    }

    maxWeight = DataType(1);
    minWeight = DataType(0);

    std::cerr << "finished\n";
  }

  if( invertWeights )
  {
    std::cerr << "* Inverting filtration weights...";

    for( auto it = K.begin(); it != K.end(); ++it )
    {
      if( it->dimension() == 0 )
        continue;

      auto s = *it;
      s.setData( maxWeight - s.data() );

      K.replace( it, s );
    }

    std::cerr << "finished\n";
  }

  // Expansion ---------------------------------------------------------

  std::cerr << "* Expanding simplicial complex to k=" << maxK << "...";

  {
    aleph::geometry::RipsExpander<SimplicialComplex> ripsExpander;
    K = ripsExpander( K, maxK );
    K = ripsExpander.assignMaximumWeight( K );
  }

  std::cerr << "finished\n"
            << "* Expanded simplicial complex has " << K.size() << " simplices\n";

  K.sort( aleph::topology::filtrations::Data<Simplex>() );

  // Pesistent homology ------------------------------------------------

  std::cerr << "* Calculating persistent homology...";

  auto persistenceDiagrams
    = aleph::calculatePersistenceDiagrams( K );

  std::cerr << "...finished\n";

  for( auto&& pd : persistenceDiagrams )
  {
    pd.removeDiagonal();

    using namespace aleph::utilities;
    auto outputFilename = "/tmp/" + stem( basename( filename ) )
                                  + "_d"
                                  + format( static_cast<decltype(maxK)>( pd.dimension() ), maxK )
                                  + ".txt";

    std::cerr << "* Storing output in '" << outputFilename << "'...\n";

    std::transform( pd.begin(), pd.end(), pd.begin(),
        [&maxWeight] ( const PersistenceDiagram::Point& p )
        {
          if( !std::isfinite( p.y() ) )
            return PersistenceDiagram::Point( p.x(), 2 * maxWeight );
          else
            return PersistenceDiagram::Point( p );
        } );

    std::ofstream out( outputFilename );
    out << "# Original filename: " << filename       << "\n";
    out << "# d                : " << pd.dimension() << "\n";
    out << pd                                        << "\n";
  }
}
