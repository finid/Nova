//============================================================================
// Name        : PersonalityTreeItem.cpp
// Copyright   : DataSoft Corporation 2011-2012
//	Nova is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   Nova is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with Nova.  If not, see <http://www.gnu.org/licenses/>.
// Description : Represents a single item in the PersonalityTree. A linked list
//		tree structure representing the hierarchy of profiles discovered
//============================================================================

#include "Profile.h"
#include "HoneydConfiguration.h"
#include <map>
#include <sstream>
#define EQUALITY_THRESHOLD 0.00001
using namespace std;

namespace Nova
{

Profile::Profile(Profile *parent, string key)
{
	m_children.clear();
	m_name = key;
	m_osclass = "";
	m_count = 0;
	m_avgPortCount = 0;
	m_parent = parent;
	m_uptimeMin = 0;	//TODO: What if we don't see any uptimes? We need to have a sane default
	m_uptimeMax = ~0;	//TODO: Let's have a more reasonable "maximum" here. We don't want to be setting random uptimes this high
	m_isPersonalityInherited = false;
	m_isUptimeInherited = false;
	m_isDropRateInherited = false;
}

Profile::Profile(string parentName, std::string key)
{
	m_children.clear();
	m_name = key;
	m_osclass = "";
	m_count = 0;
	m_avgPortCount = 0;
	m_parent = HoneydConfiguration::Inst()->GetProfile(parentName);
	m_uptimeMin = 0;	//TODO: What if we don't see any uptimes? We need to have a sane default
	m_uptimeMax = ~0;	//TODO: Let's have a more reasonable "maximum" here. We don't want to be setting random uptimes this high
	m_isPersonalityInherited = false;
	m_isUptimeInherited = false;
	m_isDropRateInherited = false;
}

Profile::~Profile()
{
	for(uint i = 0; i < m_children.size(); i++)
	{
		if(m_children[i] != NULL)
		{
			delete m_children[i];
		}
	}

	for(uint i = 0; i < m_portSets.size(); i++)
	{
		//TODO: Double delete?
		//delete m_portSets[i];
	}
}

string Profile::ToString(const std::string &portSetName, const std::string &nodeName)
{
	stringstream out;

	if(!nodeName.compare("default"))
	{
		out << "create " << nodeName << '\n';
	}
	else
	{
		out << "clone " << nodeName << " default\n";
	}

	//If the portset name is empty, just use the first port set in the list
	if(portSetName.empty())
	{
		if(!m_portSets.empty())
		{
			out << "set " << nodeName << " default tcp action " << Port::PortBehaviorToString(m_portSets[0]->m_defaultTCPBehavior) << '\n';
			out << "set " << nodeName << " default udp action " << Port::PortBehaviorToString(m_portSets[0]->m_defaultUDPBehavior) << '\n';
			out << "set " << nodeName << " default icmp action " << Port::PortBehaviorToString(m_portSets[0]->m_defaultICMPBehavior) << '\n';
		}
	}
	else
	{
		//Ports
		for(uint i = 0; i < m_portSets.size(); i++)
		{
			//If we get a match for the port set
			if(!portSetName.compare(m_portSets[i]->m_name))
			{
				out << m_portSets[i]->ToString(nodeName);
			}
		}
	}

	//Use the recursive personality, here
	string personality = this->GetPersonality();

	if((personality != "") && (personality != "NULL"))
	{
		out << "set " << nodeName << " personality \"" << personality << '"' << '\n';
	}

	//TODO: This always uses the vendor with the highest likelihood. Should get changed?
	string vendor = "";
	double maxDist = 0;
	for(uint i = 0; i < m_vendors.size(); i++)
	{
		if(m_vendors[i].second > maxDist)
		{
			maxDist = m_vendors[i].second;
			vendor = m_vendors[i].first;
		}
	}
	if(vendor.compare(""))
	{
		out << "set " << nodeName << " ethernet \"" << vendor << '"' << '\n';
	}

	if(m_dropRate.compare(""))
	{
		out << "set " << nodeName << " droprate in " << m_dropRate << '\n';
	}

	return out.str();
}

std::string Profile::GetRandomVendor()
{
	if(m_vendors.empty())
	{
		if(m_parent == NULL)
		{
			return "";
		}
		else
		{
			return m_parent->GetRandomVendor();
		}
	}

	//First pass, let's find out what the total is for all occurrences
	uint totalOccurrences = 0;
	for(uint i = 0; i < m_vendors.size(); i++)
	{
		totalOccurrences += m_vendors[i].second;
	}

	if(totalOccurrences == 0)
	{
		return "";
	}

	//Now we pick a random number between 1 and totalOccurrences
	// Not technically a proper distribution, but we'll live
	int random = (rand() % totalOccurrences) + 1;

	//Second pass, let's see who the winning MAC vendor actually is
	for(uint i = 0; i < m_vendors.size(); i++)
	{
		//Iteratively remove values from our total, until we get to zero. Then stop.
		random -= m_vendors[i].second;
		if(random <= 0)
		{
			//Winner!
			return m_vendors[i].first;
		}
	}
	return "";
}

PortSet *Profile::GetRandomPortSet()
{
	if(m_portSets.empty())
	{
		if (m_parent != NULL)
		{
			return m_parent->GetRandomPortSet();
		}
		else
		{
			return NULL;
		}
	}

	//Now we pick a random number between 1 and totalOccurrences
	// Not technically a proper distribution, but we'll live
	int random = rand() % m_portSets.size();

	return m_portSets[random];
}

PortSet *Profile::GetPortSet(std::string name)
{
	for(uint i = 0; i < m_portSets.size(); i++)
	{
		if(m_portSets[i]->m_name == name)
		{
			return m_portSets[i];
		}
	}

	return NULL;
}

uint Profile::GetVendorCount(std::string vendorName)
{
	for(uint i = 0; i < m_vendors.size(); i++)
	{
		if(!m_vendors[i].first.compare(vendorName))
		{
			return m_vendors[i].second;
		}
	}
	return 0;
}

void Profile::RecalculateChildDistributions()
{
	for(uint i = 0; i < m_children.size(); i++)
	{
		m_children[i]->m_distribution = (((double)m_children[i]->m_count) / (double)m_count) * 100;
	}
}

bool Profile::Copy(Profile *source)
{
	if(source == NULL)
	{
		return false;
	}

	m_count = source->m_count;
	m_distribution = source->m_distribution;
	m_name = source->m_name;
	m_parent = source->m_parent;
	m_uptimeMin = source->m_uptimeMin;
	m_uptimeMax = source->m_uptimeMax;
	m_osclass = source->m_osclass;
	m_personality = source->m_personality;
	m_dropRate = source->m_dropRate;
	m_vendors = source->m_vendors;
	m_isPersonalityInherited = source->m_isPersonalityInherited;
	m_isDropRateInherited = source->m_isDropRateInherited;
	m_isUptimeInherited = source->m_isUptimeInherited;

	//TODO: Maybe want to copy the port sets too (rather than pointers)
	m_portSets = source->m_portSets;

	return true;
}

std::string Profile::GetParentProfile()
{
	if(m_parent != NULL)
	{
		return m_parent->m_name;
	}
	else
	{
		return "";
	}
}

const std::string Profile::GetParentProfile() const
{
	if(m_parent != NULL)
	{
		return m_parent->m_name;
	}
	else
	{
		return "";
	}
}

uint Profile::GetUptimeMin()
{
	if(m_isUptimeInherited && (m_parent != NULL))
	{
		return m_parent->GetUptimeMin();
	}
	return m_uptimeMin;
}

const uint Profile::GetUptimeMin() const
{
	if(m_isUptimeInherited && (m_parent != NULL))
	{
		return m_parent->GetUptimeMin();
	}
	return m_uptimeMin;
}

uint Profile::GetUptimeMinNonRecursive()
{
	return m_uptimeMin;
}

const uint Profile::GetUptimeMinNonRecursive() const
{
	return m_uptimeMin;
}

bool Profile::SetUptimeMin(uint uptime)
{
	m_uptimeMin = uptime;
	return true;
}

uint Profile::GetUptimeMax()
{
	if(m_isUptimeInherited && (m_parent != NULL))
	{
		return m_parent->GetUptimeMax();
	}
	return m_uptimeMax;
}
const uint Profile::GetUptimeMax() const
{
	if(m_isUptimeInherited && (m_parent != NULL))
	{
		return m_parent->GetUptimeMax();
	}
	return m_uptimeMax;
}

uint Profile::GetUptimeMaxNonRecursive()
{
	return m_uptimeMax;
}

const uint Profile::GetUptimeMaxNonRecursive()const
{
	return m_uptimeMax;
}

bool Profile::SetUptimeMax(uint uptime)
{
	m_uptimeMax = uptime;
	return true;
}

std::string Profile::GetDropRate()
{
	if(m_isDropRateInherited && (m_parent != NULL))
	{
		return m_parent->GetDropRate();
	}
	return m_dropRate;
}
const std::string Profile::GetDropRate()const
{
	if(m_isDropRateInherited && (m_parent != NULL))
	{
		return m_parent->GetDropRate();
	}
	return m_dropRate;
}

std::string Profile::GetDropRateNonRecursive()
{
	return m_dropRate;
}

const std::string Profile::GetDropRateNonRecursive() const
{
	return m_dropRate;
}

bool Profile::SetDropRate(std::string droprate)
{
	m_dropRate = droprate;
	return true;
}

std::string Profile::GetName()
{
	return m_name;
}

std::string Profile::GetPersonality()
{
	if(m_isPersonalityInherited && (m_parent != NULL))
	{
		return m_parent->GetPersonality();
	}
	return m_personality;
}

const std::string Profile::GetPersonality() const
{
	if(m_isPersonalityInherited && (m_parent != NULL))
	{
		return m_parent->GetPersonality();
	}
	return m_personality;
}

std::string Profile::GetPersonalityNonRecursive()
{
	return m_personality;
}

const std::string Profile::GetPersonalityNonRecursive() const
{
	return m_personality;
}

void Profile::SetPersonality(std::string personality)
{
	m_personality = personality;
}

uint32_t Profile::GetCount()
{
	return m_count;
}

std::vector<std::string> Profile::GetVendors()
{
	std::vector<std::string> list;
	for(uint i = 0; i < m_vendors.size(); i++)
	{
		list.push_back(m_vendors[i].first);
	}
	return list;
}

std::vector<uint> Profile::GetVendorCounts()
{
	std::vector<uint> list;
	for(uint i = 0; i < m_vendors.size(); i++)
	{
		list.push_back(m_vendors[i].second);
	}
	return list;
}
//checks to see if two profiles are equal
bool Profile::IsEqual(const Profile &profile)
{
	if(this->m_avgPortCount != profile.m_avgPortCount)
	{
		return false;
	}
	if(this->m_count != profile.m_count)
	{
		return false;
	}
	if(std::abs(this->m_distribution - profile.m_distribution) > EQUALITY_THRESHOLD)
	{
		return false;
	}
	if(this->m_name.compare(profile.m_name) != 0)
	{
		return false;
	}
	if(this->m_osclass.compare(profile.m_osclass) != 0)
	{
		return false;
	}
	if(this->IsDropRateInherited() !=profile.IsDropRateInherited())
	{
		return false;
	}
	if(this->IsPersonalityInherited() != profile.IsPersonalityInherited())
	{
		return false;
	}
	if(this->IsUptimeInherited() !=profile.IsUptimeInherited())
	{
		return false;
	}
	if(this->GetParentProfile() !=profile.GetParentProfile())
	{
		return false;
	}
	if(this->GetPersonality().compare(profile.GetPersonality()) != 0 )
	{
		return false;
	}
	if(this->GetDropRate().compare(profile.GetDropRate()) != 0)
	{
		return false;
	}
	if(this->GetUptimeMax() != profile.GetUptimeMax())
	{
		return false;
	}
	if(this->GetUptimeMaxNonRecursive() != profile.GetUptimeMaxNonRecursive())
	{
		return false;
	}
	if(this->GetUptimeMin() != profile.GetUptimeMin())
	{
		return false;
	}
	if(this->GetUptimeMinNonRecursive() != profile.GetUptimeMinNonRecursive())
	{
		return false;
	}
	return true;
}
//pass a constant reference as opposed to a pointer
//it is faster and safer no unintended modification of the pointer
//make it a constant reference
//are these sub trees equal beginning from this profile and the passed profile
bool Profile::IsEqualRecursive(const Profile &profile)
{
	bool childNotFound = false;
	if(this->m_children.size() != profile.m_children.size())
	{
		return false;
	}
	if(this->IsEqual(profile))
	{
		for(uint i = 0; i < this->m_children.size(); i++)
		{
			for(uint j = 0; j < profile.m_children.size(); j++)
			{
				if((*this->m_children[i]).IsEqualRecursive(*profile.m_children[j]))
				{
					childNotFound = false;
					break;
				}
				else
					childNotFound = true;
			}
			if(childNotFound == true)
				return false;
		}
	}
	else
		return false;

	return true;
}

bool Profile::IsPersonalityInherited()
{
	return m_isPersonalityInherited;
}

const bool Profile::IsPersonalityInherited() const
{
	return m_isPersonalityInherited;
}

bool Profile::IsUptimeInherited()
{
	return m_isUptimeInherited;
}

const bool Profile::IsUptimeInherited() const
{
	return m_isUptimeInherited;
}

bool Profile::IsDropRateInherited()
{
	return m_isDropRateInherited;
}
const bool Profile::IsDropRateInherited() const
{
	return m_isDropRateInherited;
}
}
