#include <string.h>
#include <stdio.h>
#include <algorithm>
#include "stdint.h"
#include "myfat.h"
#include <iostream>

using namespace std;

/*
 * Author: Liam Dwyer (ljd3za)
 * Date: February 12, 2018
 * Purpose: String manipulation utilities
*/

string get_first_dir(string path) {
    path = path.substr(1);
    string delimiter = "/";
    int index = path.find(delimiter);
    if(index != string::npos) {
      string first_dir = path.substr(0, index);
      return first_dir;
    }
    else {
      return path;
    }
}

string get_remaining_path(string path) {
    path = path.substr(1);
    string delimiter = "/";
    int index = path.find(delimiter);
    if(index != string::npos) {
      return path.substr(index, path.length());
    }
    else {
      return "";
    }
}

bool nameCompare(char* dirName, string path) {
    if(path != "." && path != "..") {
      path.erase(remove(path.begin(), path.end(), '.'), path.end());
    }
    for(int i = 0; i < path.length(); i++) {
	path[i] = toupper(path[i]);
    }
    char buffer[12];
    memset(buffer, 0, 12);
    int index = 0;
    for(int i = 0; i < 11; i++) {
	if(dirName[i] != ' ') {
	    buffer[index] = dirName[i];
	    index++;
	}
    }
    string s = string(buffer);
    if(s.compare(path) == 0) {
	return true;
    }
    return false;
}

uint32_t clusNumMaker(dirEnt dir) {
    return (dir.dir_fstClusHI << 16) | dir.dir_fstClusLO;
}

char* makeDirName(string name) {
    char* dirName = new char[11];
    memset(dirName, 0, 11);
    for(int i = 0; i < 11; i++) {
	dirName[i] = ' ';
    }

    int index = 0;
    string fileName = name;
    int dotIndex = name.find(".");
    if(dotIndex != string::npos && name != "." && name != "..") {
	fileName = name.substr(0, dotIndex);
    }
    
    //cout << fileName << endl;
    for(int i = 0; i <  min((int) fileName.length(), 11); i++) {
	if(fileName[i] == '\0' || fileName[i] == '.') {
	  if(fileName == "." || fileName == "..") {
	    dirName[i] = fileName[i];
	  }
	  continue;
	}
	dirName[i] = toupper(fileName[i]);
    }
    
    
    if(dotIndex != string::npos && name != "." && name != "..") {
	string ext = name.substr(dotIndex + 1, name.length() - 1);
	int extIndex = 0;
	for(int i = 8; i < 11; i++) {
	    dirName[i] = toupper(ext[extIndex]);
	    extIndex ++;
	}
    }
    
    char* dir = dirName;
    return dirName;
}