#include <standalone/CM.hh>
#include <standalone/FPGA.hh>
#include <string>
#include <vector>
#include <boost/program_options.hpp>
#include <sstream>
#include <ApolloSM/ApolloSM.hh>
#include <syslog>
#include <sstream>

// Checks register/node values
bool checkNode(ApolloSM const * const SM, std::string const node, uint32_t const correctVal) {
  bool const GOOD = true;
  bool const BAD  = false;

  uint32_t readVal;
  if(correctVal != (readVal = SM->RegReadRegister(node))) {
    syslog(LOG_ERR, "%s is, incorrectly, %d\n", node.c_str(), readVal);
    return BAD;
  } 
  return GOOD;
}

CM::CM(std::string nameArg, boost::program_options::parsed_options PO) {

  // initialize variables
  this->name      = nameArg;
  this->ID        = std::stoi(nameArg.substr(2,1)); // Ex: if nameArg = "CM12" then ID = 12
  this->powerGood = "CM." + nameArg + ".CTRL.PWR_GOOD";
  this->powerUp   = false;
  
  // Total number of options received from config file
  int numOptions = PO.options.size();

  // Look for all options related to this CM (ie. PWRGOOD, PWRUP, FPGAs, etc.)
  for(int i = 0; i < numOptions; i++) {
    std::string nextOption = PO.options[i].string_key;

    if(0 == nextOption.compare(nameArg + ".PWRGOOD"))    // ex: CM1.PWRGOOD
      {
	// found value for power good
	this->powerGood = PO.options[i].value[0].c_str();
      }
    else if(0 == nextOption.compare(nameArg + ".PWRUP")) // ex: CM1.PWRUP
      {
	// found value for power up
	// is c_str() necessary?
	std::istringstream(PO.options[i].value[0].c_str()) >> std::boolalpha >> (this->powerUp);
      }
    else if(0 == nextOption.compare(nameArg + ".FPGA"))  // ex: CM1.FPGA
      {
	// found another FPGA
	FPGA newFPGA(PO.options[i].value[0].c_str(), this->name, PO);
	this->FPGAs.push_back(newFPGA);
      }
  }
}

CM::~CM() {
}

CM::SetUp(ApolloSM const * const SM) {
  int const wait_time = 5; // 5 is 1 second
  if(this->powerUp) {
    syslog(LOG_INFO, "Powering up %s...\n", (this->name).c_str());
    // power up
    bool success = SM->PowerUpCM(this->ID, wait_time);
    if(success) {
      syslog(LOG_INFO, "%s is powered up\n", (this->name).c_str());
    } else {
      syslog(LOG_ERR, "%s failed to power up in time\n", (this->name).c_str());
    }
    // check that powerGood is 1
    if(checkNode(SM, this->powerGood, 1)) {
      std::string str;
      std::stringstream ss;
      ss << this->powerGood;
      ss >> str;
      syslog(LOG_INFO, "%s is 1\n", str.c_str());
    }
  } else {
    syslog(LOG_INFO, "No power up required for: %s\n", (this->name).c_str());
  }

  // program FPGAs
  if(this->powerUp) {
    for(size_t f = 0; f < (this->FPGAs).size(); f++) { 
      //if((this->FPGAs[f]).program) {
      (this->FPGAs[f]).bringUp(SM);
//	syslog(LOG_INFO, "%s has program = true. Attempting to program..\n", (this->FPGAs[f]).name.c_str());
//	int const success =  0;
//	int const fail    = -1;
//	int const nofile  = -2;
//	
//	// int const programmingFailed     = 0;
//	// int const programmingSuccessful = 1;
//	// assert 0 to done bit
//	// SM->RegWriteRegister(FPGAs[f].done, programmingFailed);
//	switch(bringupCMFPGAs(SM, allCMs[i].FPGAs[f])) {
//	case success:
//	  syslog(LOG_INFO, "Bringing up %s: %s FPGA succeeded. Setting %s to 1\n", (this->name).c_str(), (this->FPGAs[f]).name.c_str(), (this->FPGAs[f]).done.c_str());
//	  // write 1 to done bit
//	  //	SM->RegWriteRegister(allCMs[i].FPGAs[f].done, programmingSuccessful);
//	  break;
//	case fail:
//	  // assert 0 to done bit (paranoid)
//	  syslog(LOG_ERR, "Bringing up %s: %s FPGA failed. Setting %s to 0\n", (this->name).c_str(), (this->FPGAs[f]).name.c_str(), (this->FPGAs[f]).done.c_str());
//	  //	SM->RegWriteRegister(allCMs[i].FPGAs[f].done, programmingFailed);
//	  break;
//	case nofile:
//	  // assert 0 to done bit (paranoid)
//	  syslog(LOG_ERR, "svf file %s does not exist for %s FPGA. Setting %s to 0\n", (this->FPGAs[f]).svfFile.c_str(), (this->FPGAs[f]).name.c_str(), (this->.FPGAs[f]).done.c_str());
//	  //	SM->RegWriteRegister(allCMs[i].FPGAs[f].done, programmingFailed);
//	  break;
//	    }
//      } else {
//	syslog(LOG_INFO, "%s will not be programmed because it has program = false\n",(this->FPGAs[f]).name.c_str());
//      }
    }
  } else {
    syslog(LOG_INFO, "%s has powerUp = false. None of its FPGAs will be powered up\n", (this->name).c_str());
  }
  
  // Print firmware build date for FPGA
  //     printBuildDate(SM, allCMs[i].ID);
}

// std::vector<FPGA> FPGAs;
//   
// std::string name;
// int ID;
// std::string powerGood;
// bool powerUp;


