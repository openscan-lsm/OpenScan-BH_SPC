#pragma once

#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>


// Object that receives finish/error from all processes and ensures everything
// completes and gets cleaned up.
// To keep things simple, this object does not know what all the processes
// are, only the number that must complete.
class AcquisitionCompletion final {
	std::mutex mutex;
	std::vector<std::string> errors;
	unsigned unfinishedCount;
	std::function<void()> cancelFunction;
	std::promise<std::vector<std::string>> signalCompletion;
	std::shared_future<std::vector<std::string>> completion;
	std::function<void(std::string const&)> logFunction;

public:
	template <typename F, typename G>
	AcquisitionCompletion(F cancelFunc, G logFunc) :
		unfinishedCount(0),
		cancelFunction(cancelFunc),
		completion(signalCompletion.get_future().share()),
		logFunction(logFunc)
	{}

	template <typename F>
	AcquisitionCompletion(F cancelFunc) :
		AcquisitionCompletion(cancelFunc, [](std::string const&) {})
	{}

	std::shared_future<std::vector<std::string>> GetCompletion() {
		return completion;
	}

	void AddProcess(std::string const& proc) {
		std::lock_guard<std::mutex> hold(mutex);
		++unfinishedCount;
		logFunction("AcquisitionCompletion: AddProcess " + proc);
	}

	void HandleError(std::string const& message, std::string const& proc) {
		bool firstError;
		bool allDone;

		{
			std::lock_guard<std::mutex> hold(mutex);
			firstError = errors.empty();
			errors.emplace_back(message);
			--unfinishedCount;
			allDone = unfinishedCount == 0;
		}
		logFunction("AcquisitionCompletion: HandleError " + proc);

		if (firstError && cancelFunction) {
			logFunction("AcquisitionCompletion: Calling cancel function");
			cancelFunction();
		}
		if (allDone) {
			logFunction("AcquisitionCompletion: Completing");
			signalCompletion.set_value(errors);
		}
	}

	void HandleFinish(std::string const& proc) {
		bool allDone;

		{
			std::lock_guard<std::mutex> hold(mutex);
			--unfinishedCount;
			allDone = unfinishedCount == 0;
		}
		logFunction("AcquisitionCompletion: HandleFinish " + proc);

		if (allDone) {
			logFunction("AcquisitionCompletion: completing");
			signalCompletion.set_value(errors);
		}
	}
};