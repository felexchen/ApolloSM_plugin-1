#include <ApolloSM/ApolloSM.hh>
#include <ApolloSM/ApolloSM_Exceptions.hh>
#include <standalone/EyeScanLink.hh>
#include <BUException/ExceptionBase.hh>
#include <string>
#include <vector>
#include <signal.h>
#include <syslog.h>  
#include <fstream>
#include <boost/program_options.hpp>
#include <standalone/daemon.hh>
#include <standalone/parseOptions.hh>

// ====================================================================================================
// Define all your defaults here
#define DEFAULT_CONFIG_FILE         "/etc/eyescan"
#define DEFAULT_RUN_DIR             "/var/www/lighttpd/"
#define DEFAULT_PID_FILE            "/var/run/eyescan.pid"
#define DEFAULT_POLLTIME_IN_MINUTES 30 
#define DEFAULT_MAX_PRESCALE        3
//#define DEFAULT_BASE_NODE           "C2C1_PHY."
//#define DEFAULT_FILE_NAME           "c2c1_phy.txt"
#define DEFAULT_PHASE_INCREMENT     0.02 // UI is from -0.5 to 0.5
#define DEFAULT_VOLTAGE_INCREMENT   2    // Voltage is from -127 to 127

// ====================================================================================================
// Parse a long string into a vector of strings
std::vector<std::string> split_string(std::string str, std::string delimiter){
  
  size_t position = 0;
  std::string token;
  std::vector<std::string> vec;
  while( (position = str.find(delimiter)) != std::string::npos) {
    token = str.substr(0, position);
    vec.push_back(token);
    str.erase(0, position+delimiter.length());
  }
  vec.push_back(str);

  return vec;
}

// ====================================================================================================
int main(int argc, char** argv) { 

  // parameters to get from command line or config file (config file itself will not be in the config file, obviously)
  std::string configFile          = DEFAULT_CONFIG_FILE;
  std::string runPath             = DEFAULT_RUN_DIR;
  std::string pidFileName         = DEFAULT_PID_FILE;
  int         polltime_in_minutes = DEFAULT_POLLTIME_IN_MINUTES;

  // parse command line and config file to set parameters
  boost::program_options::options_description fileOptions{"File"}; // for parsing config file
  boost::program_options::options_description commandLineOptions{"Options"}; // for parsing command line
  commandLineOptions.add_options()
    ("config_file",
     boost::program_options::value<std::string>(),
     "config file"); // This is the only option not also in the file option (obviously)
  setOption(&fileOptions, &commandLineOptions, "run_path" , "run path" , runPath);
  setOption(&fileOptions, &commandLineOptions, "pid_file" , "pid file" , pidFileName);
  setOption(&fileOptions, &commandLineOptions, "polltime" , "polling interval" , polltime_in_minutes);

  int totalNumConfigFileOptions = 0;
  boost::program_options::parsed_options configFilePO(&fileOptions); // compiler won't let me merely declare it configFilePO so I initialized it with fileOptions; would be nice to fix this
  boost::program_options::variables_map configFileVM; // for parsing config file
  boost::program_options::variables_map commandLineVM; // for parsing command line

  // The command line must be parsed before the config file so that we know if there is a command line specified config file 
  fprintf(stdout, "Parsing command line now\n");
  try {
    // parse command line
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, commandLineOptions), commandLineVM);
  } catch(const boost::program_options::error &ex) {
    fprintf(stderr, "Caught exception while parsing command line: %s \nTerminating eyescan\n", ex.what());       
    return -1;
  }

  // Check for non default config file
  if(commandLineVM.count("config_file")) {
    configFile = commandLineVM["config_file"].as<std::string>();
  }  
  fprintf(stdout, "config file path: %s\n", configFile.c_str());

  // Now the config file may be loaded
  fprintf(stdout, "Reading from config file now\n");
  try {
    // parse config file
    // not using loadConfig() because I need more than just the variables map when looking for CMs and FPGAs
    std::ifstream ifs{configFile};
    fprintf(stderr, "Config file \"%s\" %s\n",configFile.c_str(), (!ifs.fail()) ? "exists" : "does not exist");
    if(ifs) {
      configFilePO = boost::program_options::parse_config_file(ifs, fileOptions, true);
      boost::program_options::store(configFilePO, configFileVM);
      totalNumConfigFileOptions = configFilePO.options.size();
    }      //    configFileVM = loadConfig(configFile, fileOptions);
  } catch(const boost::program_options::error &ex) {
    fprintf(stdout, "Caught exception in function loadConfig(): %s \nTerminating eyescan\n", ex.what());        
    return -1;
  }

  // Look at the config file and command line and see if we should change the parameters from their default values
  // Only run path and pid file are needed for the next bit of code. The other parameters can and should wait until syslog is available.
  setParamValue(&runPath    , "run_path", configFileVM, commandLineVM, false);
  setParamValue(&pidFileName, "pid_file", configFileVM, commandLineVM, false);

  // ============================================================================
  // Deamon book-keeping
  // Every daemon program should have one Daemon object. Daemon class functions are functions that all daemons progams have to perform. That is why we made the class.
  Daemon eyescanDaemon;
  eyescanDaemon.daemonizeThisProgram(pidFileName, runPath);

  // ============================================================================
  // Now that syslog is available, we can continue to look at the config file and command line and determine if we should change the parameters from their default values.
  setParamValue(&polltime_in_minutes, "polltime", configFileVM, commandLineVM, true);
  
  // find all links
  std::vector<EyeScanLink> allLinks;
  for(int i = 0; i < totalNumConfigFileOptions; i++) {
    // get current option
    std::string currentOption = configFilePO.options[i].string_key;
    if(currentOption.compare("LINK.NAME") == 0) {
      // Found a link. Get its name, make a link object, add it to the vector.
      std::string linkName = configFilePO.options[i].value[0].c_str();
      EyeScanLink esl(linkName, configFilePO);
      allLinks.push_back(esl);
    }
  }

  // print all link info
  for(size_t i = 0; i < allLinks.size(); i++) {
    allLinks[i].printInfo();
  }

  // ============================================================================
  // Signal handling
  struct sigaction sa_INT,sa_TERM,old_sa;
  // struct sigaction sa_INT,sa_TERM,oldINT_sa,oldTERM_sa;
  eyescanDaemon.changeSignal(&sa_INT , &old_sa, SIGINT);
  eyescanDaemon.changeSignal(&sa_TERM, NULL   , SIGTERM);
  eyescanDaemon.SetLoop(true);

  // ============================================================================
  ApolloSM * SM = NULL;
  try{
    // Initialize ApolloSM
    SM = new ApolloSM();
    if(NULL == SM){
      syslog(LOG_ERR,"Failed to create new ApolloSM\n");
      exit(EXIT_FAILURE);
    }else{
      syslog(LOG_INFO,"Created new ApolloSM\n");      
    }
    std::vector<std::string> arg;
    arg.push_back("/opt/address_table/connections.xml");
    SM->Connect(arg);    
    // ==================================
    // More set up if needed.

    // ==================================

    syslog(LOG_INFO, "Starting main eye scan loop\n");

    while(eyescanDaemon.GetLoop()) {
      
      // start timer
      
      for(size_t i = 0; i < allLinks.size(); i++) {
	
	// 1. Eye scan and write to file.
	// // enable eye scan
	allLinks[i].enableEyeScan(SM);
	allLinks[i].scan(SM);

	// 2. Graph it with Python.
	//std::string runPython = "python eyescan.py ";
	//runPython.append(links[i][OUTFILE]);
	//system(runPython.c_str());
	// python ./root/felex/eyescan.py .txt
	
	allLinks[i].plot();

	// 3. Upload plot.
//	std::string png = links[i][OUTFILE];
//	png.pop_back();
//	png.pop_back();
//	png.pop_back();
//	png.append("png");
//	
//	std::string copy = "cp ";
//	copy.append(png);
//	copy.append("/var/www/lighttpd");
//	system(copy.c_str());
      }
      // cp basenode.png /var/www/lighttpd
      
      // 4. Sleep
      syslog(LOG_INFO, "now sleeping\n");
      for(int i = 0; i < polltime_in_minutes; i++) {
	usleep(60*1000*1000); // 1 minute
      }
    }
  }catch(BUException::exBase const & e){
    syslog(LOG_INFO,"Caught BUException: %s\n   Info: %s\n",e.what(),e.Description());
          
  }catch(std::exception const & e){
    syslog(LOG_INFO,"Caught std::exception: %s\n",e.what());
          
  }
  
  // ============================================================================
  // Clean up  
  
  // Pre-delete SM clean up stuff if nedeed.
  
  // ==================================
  if(NULL != SM) {
    delete SM;
  }

  // Restore old action of receiving SIGINT (which is to kill program) before returning 
  sigaction(SIGINT, &old_sa, NULL);
  syslog(LOG_INFO,"eyescan Daemon ended\n");

  // ============================================================================  
  return 0;
}
