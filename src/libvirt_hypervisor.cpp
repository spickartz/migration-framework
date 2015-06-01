/*
 * This file is part of migration-framework.
 * Copyright (C) 2015 RWTH Aachen University - ACS
 *
 * This file is licensed under the GNU Lesser General Public License Version 3
 * Version 3, 29 June 2007. For details see 'LICENSE.md' in the root directory.
 */

#include "libvirt_hypervisor.hpp"

#include "logging.hpp"

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <stdexcept>
#include <thread>
#include <memory>


/// TODO: Get hostname dynamically.
Libvirt_hypervisor::Libvirt_hypervisor() :
	local_host_conn(virConnectOpen("qemu:///system"))
{
	if (!local_host_conn)
		throw std::runtime_error("Failed to connect to qemu on local host.");
}

Libvirt_hypervisor::~Libvirt_hypervisor()
{
	if (virConnectClose(local_host_conn)) {
		LOG_PRINT(LOG_WARNING, "Some qemu connections have not been closed after destruction of hypervisor wrapper!");
	}
}

void Libvirt_hypervisor::start(const std::string &vm_name, unsigned int vcpus, unsigned long memory)
{
	virDomainPtr d1 = virDomainLookupByName(local_host_conn, vm_name.c_str());
	std::unique_ptr<virDomain, decltype(&virDomainFree)> domain(d1, virDomainFree);
	if (!domain)
		throw std::runtime_error("Domain not found.");
	std::unique_ptr<virDomainInfo> domain_info(new virDomainInfo);
	if (virDomainGetInfo(domain.get(), domain_info.get()) == -1)
		throw std::runtime_error("Failed getting domain info.");
	if (domain_info->state != VIR_DOMAIN_SHUTOFF)
		throw std::runtime_error("Wrong domain state: " + std::to_string(domain_info->state));
	if (virDomainSetMemoryFlags(domain.get(), memory, VIR_DOMAIN_AFFECT_CURRENT | VIR_DOMAIN_MEM_MAXIMUM) == -1)
		throw std::runtime_error("Error setting maximum amount of memory to " + std::to_string(memory) + " KiB for domain " + vm_name);
	if (virDomainSetMemoryFlags(domain.get(), memory, VIR_DOMAIN_AFFECT_CURRENT) == -1)
		throw std::runtime_error("Error setting amount of memory to " + std::to_string(memory) + " KiB for domain " + vm_name);
	if (virDomainSetVcpusFlags(domain.get(), vcpus, VIR_DOMAIN_AFFECT_CURRENT) == -1)
		throw std::runtime_error("Error setting number of vcpus to " + std::to_string(vcpus) + " for domain " + vm_name);
	if (virDomainCreate(domain.get()) == -1)
		throw std::runtime_error(std::string("Error creating domain: ") + virGetLastErrorMessage());
}

void Libvirt_hypervisor::stop(const std::string &vm_name)
{
	virDomainPtr d1 = virDomainLookupByName(local_host_conn, vm_name.c_str());
	std::unique_ptr<virDomain, decltype(&virDomainFree)> domain(d1, virDomainFree);
	if (!domain)
		throw std::runtime_error("Domain not found.");
	std::unique_ptr<virDomainInfo> domain_info(new virDomainInfo);
	if (virDomainGetInfo(domain.get(), domain_info.get()) == -1)
		throw std::runtime_error("Failed getting domain info.");
	if (domain_info->state != VIR_DOMAIN_RUNNING)
		throw std::runtime_error("Domain not running.");
	if (virDomainDestroy(domain.get()) == -1)
		throw std::runtime_error("Error destroying domain.");
}

void Libvirt_hypervisor::migrate(const std::string &vm_name, const std::string &dest_hostname, bool live_migration)
{
	virDomainPtr d1 = virDomainLookupByName(local_host_conn, vm_name.c_str());
	std::unique_ptr<virDomain, decltype(&virDomainFree)> domain(d1, virDomainFree);
	if (!domain)
		throw std::runtime_error("Domain not found.");
	std::unique_ptr<virDomainInfo> domain_info(new virDomainInfo);
	if (virDomainGetInfo(domain.get(), domain_info.get()) == -1)
		throw std::runtime_error("Failed getting domain info.");
	if (domain_info->state != VIR_DOMAIN_RUNNING)
		throw std::runtime_error("Domain not running.");
	virConnectPtr c1 = virConnectOpen(("qemu+ssh://" + dest_hostname + "/system").c_str());
	std::unique_ptr<virConnect, decltype(&virConnectClose)> dest_connection(c1, virConnectClose);
	if (!dest_connection)
		throw std::runtime_error("Cannot establish connection to " + dest_hostname);
	unsigned long flags = 0;
	flags |= live_migration ? VIR_MIGRATE_LIVE : 0;
	virDomainPtr d2 = virDomainMigrate(domain.get(), dest_connection.get(), flags, 0, 0, 0);
	std::unique_ptr<virDomain, decltype(&virDomainFree)> dest_domain(d2, virDomainFree);
	if (!dest_domain)
		throw std::runtime_error(std::string("Migration failed: ") + virGetLastErrorMessage());
}
