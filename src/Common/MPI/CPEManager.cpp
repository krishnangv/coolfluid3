// Copyright (C) 2010 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include <boost/assign/std/vector.hpp> // for 'operator+=()'

#include "rapidxml/rapidxml.hpp"

#include "Common/BasicExceptions.hpp"
#include "Common/BoostFilesystem.hpp"
#include "Common/CBuilder.hpp"
#include "Common/LibCommon.hpp"
#include "Common/OptionT.hpp"
#include "Common/OptionURI.hpp"
#include "Common/Signal.hpp"

#include "Common/XML/FileOperations.hpp"
#include "Common/XML/SignalOptions.hpp"

#include "Common/MPI/PE.hpp"
#include "Common/MPI/ListeningThread.hpp"

#include "Common/MPI/CPEManager.hpp"
#include "Common/MPI/CWorkerGroup.hpp"

#include "Common/Log.hpp"

using namespace boost::assign; // bring 'operator+=()' into scope
using namespace CF::Common::XML;

////////////////////////////////////////////////////////////////////////////

namespace CF {
namespace Common {
namespace mpi {

////////////////////////////////////////////////////////////////////////////

Common::ComponentBuilder < CPEManager, Component, LibCommon > CPEManager_Builder;

////////////////////////////////////////////////////////////////////////////

CPEManager::CPEManager ( const std::string & name )
  : Component(name)
{
  m_listener = new ListeningThread();

  if( PE::instance().get_parent() != MPI::COMM_NULL )
  {
    m_groups["MPI_Parent"] = PE::instance().get_parent();
    m_listener->add_communicator( PE::instance().get_parent() );
    m_listener->start_listening();
  }

  regist_signal( "spawn_group" )
      ->description("Creates a new group of workers")
      ->pretty_name("Spawn new group")
      ->connect( boost::bind(&CPEManager::signal_spawn_group, this, _1) );

  regist_signal( "kill_group" )
      ->description("Kills a group of workers")
      ->pretty_name("Kill group")
      ->connect( boost::bind(&CPEManager::signal_kill_group, this, _1) );

  regist_signal( "kill_all" )
      ->description("Kills all groups of workers")
      ->hidden(true)
      ->pretty_name("Kill all groups")
      ->connect( boost::bind(&CPEManager::signal_kill_all, this, _1) );

  regist_signal("exit")
      ->connect( boost::bind(&CPEManager::signal_exit, this, _1) )
      ->hidden(true)
      ->description( "Stops the listening thread" );

  regist_signal("forward_signal")
      ->hidden(true)
      ->description("Called when there is a signal to forward");

  regist_signal( "message" )
      ->description("New message has arrived from a worker")
      ->pretty_name("")
      ->connect( boost::bind(&CPEManager::signal_message, this, _1) );

  regist_signal( "signal_to_forward" )
      ->description("Signal called by this object when to forward a signal "
                    "called from a worker.");

  signal("spawn_group")->signature( boost::bind(&CPEManager::signature_spawn_group, this, _1) );
  signal("kill_group")->signature( boost::bind(&CPEManager::signature_kill_group, this, _1) );

  signal("create_component")->hidden(true);
  signal("rename_component")->hidden(true);
  signal("delete_component")->hidden(true);
  signal("move_component")->hidden(true);
  signal("message")->hidden(true);
  signal("signal_to_forward")->hidden(true);

  m_listener->new_signal.connect( boost::bind(&CPEManager::new_signal, this, _1, _2) );
}

////////////////////////////////////////////////////////////////////////////

CPEManager::~CPEManager ()
{

}

////////////////////////////////////////////////////////////////////////////

void CPEManager::new_signal ( const MPI::Intercomm &, XML::XmlDoc::Ptr sig)
{
  if( PE::instance().instance().get_parent() == MPI_COMM_NULL )
  {
    SignalFrame frame( sig );
    call_signal( "signal_to_forward", frame );
//    signal_to_forward( frame );

//    std::cout << "Received a reply" << std::endl;
//    std::string str;
//    to_string( *sig.get(), str);
//    std::cout << str << std::endl;
  }
  else if( !m_root.expired() )
  {
    CRoot::Ptr root = m_root.lock();

    try
    {
      XmlNode nodedoc = Protocol::goto_doc_node(*sig.get());
      SignalFrame signal_frame( sig );
//      SignalFrame signal_frame( nodedoc.content->first_node() );
      rapidxml::xml_attribute<>* tmpAttr = signal_frame.node.content->first_attribute("target");
//      bool

      if( is_null(tmpAttr) )
        throw ValueNotFound(FromHere(), "Could not find the target.");

      std::string target = tmpAttr->value();

      tmpAttr = signal_frame.node.content->first_attribute("receiver");

      if( is_null(tmpAttr) )
        throw ValueNotFound(FromHere(), "Could not find the receiver.");

      std::string str;
      to_string( signal_frame.node, str);

      Component::Ptr comp = root->retrieve_component_checked( tmpAttr->value() );

      comp->call_signal(target, signal_frame);

      if( PE::instance().rank() == 0 ) // only rank 0 sends the reply
      {
        SignalFrame reply = signal_frame.get_reply();

        if( reply.node.is_valid() && !reply.node.attribute_value("target").empty() )
        {
//          std::string str;
//          to_string(reply.node, str);
//          std::cout << "Sending \n" << str << "\nas a reply" << std::endl;

          send_to_parent( signal_frame );
//          m_core->sendSignal( *signal_frame.xml_doc.get() );
//          m_journal->add_signal( reply );
        }
      }

//      std::cout << "Executing \n" << str << "\n on component " << tmpAttr->value()
//             << " (rank " << PE::instance().rank()<< ")." << std::endl;

      // synchronize with other buddies
      PE::instance().barrier();

      if( PE::instance().rank() == 0 )
      {
        /// @todo change the receiver path to be not hardcoded
        SignalFrame frame("ack", uri(), "//Root/UI/NetworkQueue");
        SignalOptions & options = frame.options();
        std::string frameid = signal_frame.node.attribute_value("frameid");

        options.add_option< OptionT<std::string> >("frameid", frameid );
        options.add_option< OptionT<bool> >("success", true );
        options.add_option< OptionT<std::string> >("message", "" );

        options.flush();

        send_to_parent( frame );
//        m_commServer->sendSignalToClient( *frame.xml_doc.get(), clientid);

      }

    }
    catch( Exception & cfe )
    {
      CFerror << cfe.what() << CFendl;
    }
    catch( std::exception & stde )
    {
      CFerror << stde.what() << CFendl;
    }
    catch(...)
    {
      CFerror << "Unhandled exception." << CFendl;
    }

  }

}

////////////////////////////////////////////////////////////////////////////

void CPEManager::spawn_group ( const std::string & name,
                               Uint nb_workers,
                               const char * command,
                               const std::string & forward,
                               const char * hosts )
{
  if( m_groups.find(name) != m_groups.end())
    throw ValueExists(FromHere(), "A group of name " + name + " already exists.");

  boost::filesystem::path path;
  std::string forw = "--forward=" + forward;

  path = boost::filesystem::system_complete( command );

  // MPI wants the arguments to be 'char *' and not 'const char *'
  // thus, we need to make a copy since std::string::c_str() returns a const.
  char * forw_cstr = new char[forw.length() + 1];

  std::strcpy( forw_cstr, forw.c_str() );

  char * args[] = { forw_cstr, nullptr };

  Communicator comm = PE::instance().spawn(nb_workers, command, args, hosts);
  m_groups[name] = comm;
  m_listener->add_communicator( comm );

  CWorkerGroup & wg = create_component<CWorkerGroup>(name);
  wg.set_communicator(comm);
  wg.mark_basic();

  PE::instance().barrier( comm );

  // if it is the first group, we start listening
  if( m_groups.size() == 1 )
    m_listener->start_listening();


  delete forw_cstr;
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::kill_group ( const std::string & name )
{
  SignalFrame frame("exit", uri(), uri());
  std::map<std::string, Communicator>::iterator it = m_groups.find(name);

  if( it == m_groups.end() )
    throw ValueNotFound(FromHere(), "Group [" + name + "] does not exist.");

  send_to( it->second, frame );

  // workers have a barrier on their parent comm just before calling MPI_Finalize
  PE::instance().barrier( it->second );
  m_listener->remove_comunicator( it->second );

  m_groups.erase(it);

  // if there are no groups anymore, we stop the listening
  if( m_groups.empty() )
    m_listener->stop_listening();

  remove_component(it->first);

  CFinfo << "Group " << name << " was killed." << CFendl;
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::kill_all ()
{
  // stop the thread and wait() first
  /// @todo implement
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::wait ()
{
  /// @todo implement
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::send_to_parent ( const SignalArgs & args )
{
  send_to("MPI_Parent", args);
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::send_to ( const std::string & group, const SignalArgs & args )
{
  std::map<std::string, Communicator>::iterator it = m_groups.find(group);

  if( it == m_groups.end() )
    throw ValueNotFound(FromHere(), "Group [" + group + "] does not exist.");

  send_to( it->second, args );
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::broadcast ( const SignalArgs & args )
{
  std::map<std::string, Communicator>::iterator it = m_groups.begin();

  for( ; it != m_groups.end() ; ++it )
    send_to( it->second, args );
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::send_to ( Communicator comm, const SignalArgs &args )
{
  std::string str;
  char * buffer;
  int remote_size;

  cf_assert( is_not_null(args.xml_doc) );

  to_string( *args.xml_doc, str);

  buffer = new char[ str.length() + 1 ];
  std::strcpy( buffer, str.c_str() );

  MPI_Comm_remote_size(comm, &remote_size);

//  std::cout << "Worker[" << PE::instance().rank() << "]" << " -> Sending " << buffer << std::endl;

  for(int i = 0 ; i < remote_size ; ++i)
    MPI_Send( buffer, str.length() + 1, MPI_CHAR, i, 0, comm );

  delete [] buffer;
}

////////////////////////////////////////////////////////////////////////////

boost::thread & CPEManager::listening_thread ()
{
  return m_listener->thread();
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::signal_spawn_group ( SignalArgs & args )
{
  SignalOptions options( args );
  const char * cmd = "../Tools/Solver/coolfluid-solver";

  Uint nb_workers = options.value<Uint>("count");
  std::string name = options.value<std::string>("name");
  std::string forward = options.value<std::string>("log_forwarding");

  if(forward == "None")
    forward = "none";
  else if (forward == "Only rank 0")
    forward = "rank0";
  else if (forward == "All ranks")
    forward = "all";
  else
    throw ValueNotFound(FromHere(), "Unknown forward type [" + forward + "]");

  spawn_group(name, nb_workers, cmd, forward);
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::signal_kill_group ( SignalArgs & args )
{
  SignalOptions options(args);
  std::string group_name = options.value<std::string>("group");

  kill_group( group_name );
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::signal_kill_all ( SignalArgs & args )
{

}

////////////////////////////////////////////////////////////////////////////

void CPEManager::signal_message ( SignalArgs & args )
{
  SignalOptions options(args);

  std::string msg = options.value<std::string>("message");

//  CFinfo << msg << CFendl;
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::mpi_forward ( SignalArgs & args )
{
  XmlDoc::Ptr doc = Protocol::create_doc();
  XmlNode node = Protocol::goto_doc_node( *doc.get() );
  XmlNode sig_node = node.add_node( "tmp" );

  node.deep_copy( sig_node );
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::signal_exit ( SignalArgs & args )
{
  m_listener->stop_listening();
}

////////////////////////////////////////////////////////////////////////////

void CPEManager::signature_spawn_group ( SignalArgs & args )
{
  SignalOptions options( args );

  options.add_option< OptionT<std::string> >("name", std::string())
      ->pretty_name("Name")
      ->description("Name of the new group");

  options.add_option< OptionT<Uint> >("count", Uint(1))
      ->pretty_name("Workers Count")
      ->description("Number of workers to spawn.");

  options.add_option< OptionT<std::string> >("log_forwarding", std::string("None") )
      ->pretty_name("Log Forwarding")
      ->description("Defines the way the log is forwarded from the workers.")
      ->restricted_list() += std::string("Only rank 0"), std::string("All ranks");

}

////////////////////////////////////////////////////////////////////////////

void CPEManager::signature_kill_group ( SignalArgs & args )
{
  SignalOptions options( args );
  std::vector<boost::any> groups( m_groups.size() - 1 );
  std::map<std::string, Communicator>::iterator it = m_groups.begin();

  if(m_groups.empty())
    throw IllegalCall(FromHere(), "There are no groups to kill.");

  for(int i = 0 ; it != m_groups.end() ; ++it, ++i )
    groups[i] = it->first;

  options.add_option< OptionT<std::string> >("group", m_groups.begin()->first )
      ->pretty_name("Group to kill")
      ->description("Processes belonging to the selected group will be exited.")
      ->restricted_list() = groups;
}

////////////////////////////////////////////////////////////////////////////

} // mpi
} // Common
} // CF
