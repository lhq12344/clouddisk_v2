#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "consul.h"
#include <grpcpp/grpcpp.h>

namespace ArcGrpcLB
{

	template <typename Service>
	struct Endpoint
	{
		std::string address;
		int port{0};
		std::shared_ptr<grpc::Channel> channel;
		std::shared_ptr<typename Service::Stub> stub;

		std::string addrStr() const { return address + ":" + std::to_string(port); }
	};

	template <typename Service>
	struct Entry
	{
		std::vector<Endpoint<Service>> eps;
		std::atomic<uint64_t> rr{0};
		std::chrono::steady_clock::time_point expire_at{};
		mutable std::shared_mutex mu;
	};

	template <typename Service>
	static inline std::shared_ptr<typename Service::Stub> MakeSharedStub(const std::shared_ptr<grpc::Channel> &ch)
	{
		auto up = Service::NewStub(ch);
		return std::shared_ptr<typename Service::Stub>(up.release(),
													   [](typename Service::Stub *p)
													   { delete p; });
	}

	static inline bool IsExpired(const std::chrono::steady_clock::time_point &tp)
	{
		return tp != std::chrono::steady_clock::time_point{} &&
			   std::chrono::steady_clock::now() > tp;
	}

	template <typename Service, typename ReadyFn>
	std::shared_ptr<typename Service::Stub> PickReadyStubRR(const std::shared_ptr<Entry<Service>> &entry,
															const std::string &key,
															ReadyFn &&isReady)
	{
		std::shared_lock lk(entry->mu);

		if (entry->eps.empty() || IsExpired(entry->expire_at))
			return nullptr;

		const size_t n = entry->eps.size();
		uint64_t base = entry->rr.fetch_add(1, std::memory_order_relaxed);

		for (size_t i = 0; i < n; ++i)
		{
			size_t idx = (base + i) % n;
			auto &ep = entry->eps[idx];

			if (!ep.channel)
			{
				ep.channel = grpc::CreateChannel(ep.addrStr(), grpc::InsecureChannelCredentials());
				ep.stub = MakeSharedStub<Service>(ep.channel);
			}

			if (ep.channel && isReady(ep.channel))
			{
				LOG_INFO("[LB] key={}, pick {} (idx={}/{})", key, ep.addrStr(), idx, n);
				return ep.stub;
			}
		}
		return nullptr;
	}

	template <typename Service>
	void RefreshEntry(const std::shared_ptr<Entry<Service>> &entry,
					  const std::vector<BasicInstance> &instances,
					  int ttlSeconds)
	{
		std::unique_lock lk(entry->mu);

		// 差分复用：按 address:port 复用旧连接
		std::vector<Endpoint<Service>> old;
		old.swap(entry->eps);

		std::unordered_map<std::string, Endpoint<Service>> oldMap;
		oldMap.reserve(old.size() * 2 + 1);
		for (auto &ep : old)
			oldMap.emplace(ep.addrStr(), std::move(ep));

		entry->eps.clear();
		entry->eps.reserve(instances.size());

		for (const auto &ins : instances)
		{
			Endpoint<Service> ep;
			ep.address = ins.address;
			ep.port = ins.port;

			auto k = ep.addrStr();
			auto it = oldMap.find(k);
			if (it != oldMap.end())
			{
				ep.channel = std::move(it->second.channel);
				ep.stub = std::move(it->second.stub);
			}
			entry->eps.push_back(std::move(ep));
		}

		entry->expire_at = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);
	}

	// ArcCache 需要支持：get(key, Value&) / put(key, Value)
	// Value = std::shared_ptr<Entry<Service>>
	template <typename Service, typename ArcCache, typename ReadyFn>
	std::shared_ptr<typename Service::Stub> FindService(ArcCache &cache,
														CloudiskConsul &consul,
														const std::string &key,
														int ttlSeconds,
														ReadyFn &&isReady)
	{
		std::shared_ptr<Entry<Service>> entry;

		if (cache.get(key, entry) && entry)
		{
			if (auto stub = PickReadyStubRR<Service>(entry, key, std::forward<ReadyFn>(isReady)))
			{
				return stub;
			}
			LOG_WARN("[FindService] cache hit but no ready stub, key={}", key);
		}
		else
		{
			LOG_INFO("[FindService] cache miss, key={}", key);
		}

		auto instances = consul.getAllPassingInstances(key);
		if (instances.empty())
		{
			LOG_ERROR("[FindService] No available instance for {}", key);
			return nullptr;
		}

		if (!entry)
			entry = std::make_shared<Entry<Service>>();
		RefreshEntry<Service>(entry, instances, ttlSeconds);
		cache.put(key, entry);

		if (auto stub = PickReadyStubRR<Service>(entry, key, std::forward<ReadyFn>(isReady)))
		{
			return stub;
		}

		LOG_ERROR("[FindService] Instances exist but none ready for {}", key);
		return nullptr;
	}

} // namespace ArcGrpcLB
