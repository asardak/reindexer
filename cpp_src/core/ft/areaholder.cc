#include "areaholder.h"

namespace reindexer {
using std::vector;

// Maximum highlighted areas in eash result
const int kMaxAreasInResult = 5;

AreaHolder::~AreaHolder() {}

void AreaHolder::Reserve(int size) { areas.reserve(size); }

void AreaHolder::AddTreeGramm(int pos, int filed) {
	Area thr_area(0, 0);
	if (pos < space_size_) {
		thr_area.start_ = 0;
		thr_area.end_ = pos + (buffer_size_ - 1) - space_size_;
	} else if (pos > (total_size_ - space_size_)) {
		thr_area.start_ = pos - space_size_;
		thr_area.end_ = buffer_size_ - 1 + total_size_ - (2 * space_size_);
	} else {
		thr_area.start_ = pos - space_size_;
		thr_area.end_ = pos - space_size_ + (buffer_size_ - 1);
	}

	insertArea(thr_area, filed);
}

void AreaHolder::Commit() {
	commited_ = true;
	for (auto &area : areas) {
		sort(area.begin(), area.end(), [](const Area &rhs, const Area &lhs) { return rhs.start_ < lhs.start_; });
		if (!area.empty()) {
			for (auto vit = area.begin() + 1; vit != area.end(); ++vit) {
				auto prev = vit - 1;
				if (vit->Concat(*prev)) {
					vit = area.erase(prev);
				}
			}
		}
	}
}
bool AreaHolder::AddWord(int start_pos, int size, int filed) {
	Area thr_area{start_pos, start_pos + size};
	return insertArea(thr_area, filed);
}
bool AreaHolder::insertArea(const Area &area, int field) {
	commited_ = false;
	if (areas.size() <= size_t(field)) areas.resize(field + 1);
	if (areas[field].empty() || !areas[field].back().Concat(area)) {
		if (areas[field].size() >= kMaxAreasInResult) return false;
		areas[field].push_back(area);
	}
	return true;
}

AreaVec AreaHolder::GetSnippet(int field, int front, int back, int total_size) {
	if (!commited_) Commit();
	auto va = areas[field];
	for (auto &area : va) {
		area.start_ -= front;
		if (area.start_ < 0 || front < 0) area.start_ = 0;
		area.end_ += back;
		if (area.end_ > total_size || back < 0) area.end_ = total_size;
	}
	if (!va.empty()) {
		for (auto vit = va.begin() + 1; vit != va.end(); ++vit) {
			auto prev = vit - 1;
			if (vit->Concat(*prev)) {
				vit = va.erase(prev);
			}
		}
	}
	return va;
}
void AreaHolder::ReserveField(int size) { areas.resize(size); }
AreaVec *AreaHolder::GetAreas(int field) {
	if (!commited_) Commit();
	if (areas.size() <= size_t(field)) return nullptr;
	return &areas[field];
}
}  // namespace reindexer
