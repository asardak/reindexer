#include "ft_fixture.h"

#include <fstream>
#include <iterator>

#include "tools/stringstools.h"

using benchmark::State;
using benchmark::AllocsTracker;

using std::ifstream;
using std::istream_iterator;
using std::back_inserter;

using reindexer::Query;
using reindexer::QueryResults;
using reindexer::utf8_to_utf16;
using reindexer::utf16_to_utf8;

#define DATA_PATH "dict.txt"

uint8_t printFlags = AllocsTracker::kPrintAllocs | AllocsTracker::kPrintHold;

FullText::FullText(Reindexer* db, const string& name, size_t maxItems) : BaseFixture(db, name, maxItems, 1, false) {
	AddIndex("id", "id", "hash", "int", IndexOpts().PK());
	AddIndex("description", "description", "-", "string", IndexOpts());
	AddIndex("year", "year", "tree", "int", IndexOpts());
	AddIndex("countries", "countries", "tree", "string", IndexOpts().Array());

	AddIndex("countries+description=searchfast", "", "text", "composite", IndexOpts());
	AddIndex("countries+description=searchfuzzy", "", "fuzzytext", "composite", IndexOpts());
}

reindexer::Error FullText::Initialize() {
	auto err = BaseFixture::Initialize();
	if (!err.ok()) return err;

	ifstream file;
	file.open(DATA_PATH);

	if (!file) return Error(errNotValid, strerror(errno));
	words_.reserve(140000);
	std::copy(istream_iterator<string>(file), istream_iterator<string>(), back_inserter(words_));

	// clang-format off
	countries_ = {
		"Afghanistan", "Albania", "Algeria", "Andorra", "Angola", "Antigua & Deps",  "Argentina", "Armenia",
		"Australia", "Austria", "Azerbaijan", "Bahamas", "Bahrain", "Bangladesh", "Barbados", "Belarus",
		"Belgium", "Belize", "Benin", "Bhutan", "Bolivia", "Bosnia Herzegovina", "Botswana", "Brazil",
		"Brunei", "Bulgaria", "Burkina", "Burundi", "Cambodia", "Cameroon", "Canada", "Cape Verde"
		"Central African Rep", "Chad", "Chile", "China", "Colombia", "Comoros", "Congo", "Congo {Democratic Rep}",
		"Costa Rica", "Croatia", "Cuba", "Cyprus", "Czech Republic", "Denmark", "Djibouti", "Dominica",
		"Dominican Republic", "East Timor", "Ecuador", "Egypt", "El Salvador", "Equatorial Guinea", "Eritrea", "Estonia",
		"Ethiopia", "Fiji", "Finland", "France", "Gabon", "Gambia", "Georgia", "Germany",
		"Ghana", "Greece", "Grenada", "Guatemala", "Guinea", "Guinea-Bissau", "Guyana", "Haiti",
		"Honduras", "Hungary", "Iceland", "India", "Indonesia", "Iran", "Iraq", "Ireland {Republic}",
		"Israel", "Italy", "Ivory Coast", "Jamaica", "Japan", "Jordan", "Kazakhstan", "Kenya",
		"Kiribati", "Korea North", "Korea South", "Kosovo", "Kuwait", "Kyrgyzstan", "Laos", "Latvia",
		"Lebanon", "Lesotho", "Liberia", "Libya", "Liechtenstein", "Lithuania", "Luxembourg", "Macedonia",
		"Madagascar", "Malawi", "Malaysia", "Maldives", "Mali", "Malta", "Marshall Islands", "Mauritania",
		"Mauritius", "Mexico", "Micronesia", "Moldova", "Monaco", "Mongolia", "Montenegro", "Morocco",
		"Mozambique", "Myanmar, {Burma}", "Namibia", "Nauru", "Nepal", "Netherlands", "New Zealand",
		"Nicaragua", "Niger", "Nigeria", "Norway", "Oman", "Pakistan", "Palau", "Panama",
		"Papua New Guinea", "Paraguay", "Peru", "Philippines", "Poland", "Portugal", "Qatar", "Romania",
		"Russian Federation", "Rwanda", "St Kitts & Nevis", "St Lucia", "Saint Vincent & the Grenadines", "Samoa", "San Marino", "Sao Tome & Principe",
		"Saudi Arabia", "Senegal", "Serbia", "Seychelles", "Sierra Leone", "Singapore", "Slovakia", "Slovenia",
		"Solomon Islands", "Somalia", "South Africa", "South Sudan", "Spain", "Sri Lanka", "Sudan", "Suriname",
		"Swaziland", "Sweden", "Switzerland", "Syria", "Taiwan", "Tajikistan", "Tanzania", "Thailand",
		"Togo", "Tonga", "Trinidad & Tobago", "Tunisia", "Turkey", "Turkmenistan", "Tuvalu", "Uganda",
		"Ukraine", "United Arab Emirates", "United Kingdom", "United States", "Uruguay", "Uzbekistan", "Vanuatu", "Vatican City",
		"Venezuela", "Vietnam", "Yemen", "Zambia", "Zimbabwe"
	};
	// clang-format on
	return 0;
}

void FullText::RegisterAllCases() {
	Register("Insert", &FullText::Insert, this)->Iterations(id_seq_->Count())->Unit(benchmark::kMicrosecond);

	Register("BuildCommonIndexes", &FullText::BuildCommonIndexes, this)->Iterations(1)->Unit(benchmark::kMicrosecond);
	Register("BuildFastTextIndex", &FullText::BuildFastTextIndex, this)->Iterations(1)->Unit(benchmark::kMicrosecond);
	Register("BuildFuzzyTextIndex", &FullText::BuildFuzzyTextIndex, this)->Iterations(1)->Unit(benchmark::kMicrosecond);

	Register("Fast1WordMatch", &FullText::Fast1WordMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fast2WordsMatch", &FullText::Fast2WordsMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fast1PrefixMatch", &FullText::Fast1PrefixMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fast2PrefixMatch", &FullText::Fast2PrefixMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fast1SuffixMatch", &FullText::Fast1SuffixMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fast2SuffixMatch", &FullText::Fast2SuffixMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fast1TypoWordMatch", &FullText::Fast1TypoWordMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fast2TypoWordMatch", &FullText::Fast2TypoWordMatch, this)->Unit(benchmark::kMicrosecond);

	Register("Fuzzy1WordMatch", &FullText::Fuzzy1WordMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fuzzy2WordsMatch", &FullText::Fuzzy2WordsMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fuzzy1PrefixMatch", &FullText::Fuzzy1PrefixMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fuzzy2PrefixMatch", &FullText::Fuzzy2PrefixMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fuzzy1SuffixMatch", &FullText::Fuzzy1SuffixMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fuzzy2SuffixMatch", &FullText::Fuzzy2SuffixMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fuzzy1TypoWordMatch", &FullText::Fuzzy1TypoWordMatch, this)->Unit(benchmark::kMicrosecond);
	Register("Fuzzy2TypoWordMatch", &FullText::Fuzzy2TypoWordMatch, this)->Unit(benchmark::kMicrosecond);
}

reindexer::Item FullText::MakeItem() {
	auto item = db_->NewItem(nsdef_.name);
	item.Unsafe(false);

	auto phrase = CreatePhrase();
	auto countries = GetRandomCountries();
	raw_data_sz_ += phrase.size();
	for (auto& c : countries) raw_data_sz_ += c.size();

	item["id"] = id_seq_->Next();
	item["description"] = phrase;
	item["year"] = random<int>(2000, 2049);
	item["countries"] = toArray<string>(countries);

	return item;
}

void FullText::Insert(State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	for (auto _ : state) {
		auto item = MakeItem();
		if (!item.Status().ok()) state.SkipWithError(item.Status().what().c_str());

		auto err = db_->Insert(nsdef_.name, item);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
	}

	auto err = db_->Commit(nsdef_.name);
	if (!err.ok()) state.SkipWithError(err.what().c_str());
}

void FullText::BuildCommonIndexes(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	for (auto _ : state) {
		Query q(nsdef_.name);
		q.Where("year", CondRange, {2010, 2016}).Limit(20).Sort("year", false);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
	}
}

void FullText::BuildFastTextIndex(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t mem = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);
		q.Where("searchfast", CondEq, words_.at(random<size_t>(0, words_.size() - 1))).Limit(20);

		QueryResults qres;

		mem = get_alloc_size();
		auto err = db_->Select(q, qres);
		mem = get_alloc_size() - mem;

		if (!err.ok()) state.SkipWithError(err.what().c_str());
	}
	double ratio = mem / double(raw_data_sz_);
	state.SetLabel("Commit ratio: " + std::to_string(ratio));
}

void FullText::BuildFuzzyTextIndex(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t mem = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);
		q.Where("searchfuzzy", CondEq, words_.at(random<size_t>(0, words_.size() - 1))).Limit(20);

		QueryResults qres;

		mem = get_alloc_size();
		auto err = db_->Select(q, qres);
		mem = get_alloc_size() - mem;

		if (!err.ok()) state.SkipWithError(err.what().c_str());
	}
	double ratio = mem / double(raw_data_sz_);
	state.SetLabel("Commit ratio: " + std::to_string(ratio));
}

void FullText::Fast1WordMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		q.Where("searchfast", CondEq, words_.at(random<size_t>(0, words_.size() - 1)));

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fast2WordsMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);
		string words = words_.at(random<size_t>(0, words_.size() - 1)) + " " + words_.at(random<size_t>(0, words_.size() - 1));

		q.Where("searchfast", CondEq, words);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fuzzy1WordMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		q.Where("searchfuzzy", CondEq, words_.at(random<size_t>(0, words_.size() - 1)));

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fuzzy2WordsMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);
		string words = words_.at(random<size_t>(0, words_.size() - 1)) + " " + words_.at(random<size_t>(0, words_.size() - 1));

		q.Where("searchfuzzy", CondEq, words);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fast1PrefixMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		auto word = MakePrefixWord();
		q.Where("searchfast", CondEq, word);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fast2PrefixMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		auto words = MakePrefixWord() + " " + MakePrefixWord();
		q.Where("searchfast", CondEq, words);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fuzzy1PrefixMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		auto word = MakePrefixWord();
		q.Where("searchfuzzy", CondEq, word);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fuzzy2PrefixMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string words = MakePrefixWord() + " " + MakePrefixWord();
		q.Where("searchfuzzy", CondEq, words);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fast1SuffixMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string word = MakeSuffixWord();
		q.Where("searchfast", CondEq, word);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fast2SuffixMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string words = MakeSuffixWord() + " " + MakeSuffixWord();
		q.Where("searchfast", CondEq, words);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fuzzy1SuffixMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string word = MakeSuffixWord();
		q.Where("searchfuzzy", CondEq, word);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fuzzy2SuffixMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string words = MakeSuffixWord() + " " + MakeSuffixWord();
		q.Where("searchfuzzy", CondEq, words);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fast1TypoWordMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string word = MakeTypoWord();
		q.Where("searchfast", CondEq, word);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fast2TypoWordMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string words = MakeTypoWord() + " " + MakeTypoWord();
		q.Where("searchfast", CondEq, words);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fuzzy1TypoWordMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string word = MakeTypoWord();
		q.Where("searchfuzzy", CondEq, word);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

void FullText::Fuzzy2TypoWordMatch(benchmark::State& state) {
	AllocsTracker allocsTracker(state, printFlags);
	size_t cnt = 0;
	for (auto _ : state) {
		Query q(nsdef_.name);

		string words = MakeTypoWord() + " " + MakeTypoWord();
		q.Where("searchfuzzy", CondEq, words);

		QueryResults qres;
		auto err = db_->Select(q, qres);
		if (!err.ok()) state.SkipWithError(err.what().c_str());
		cnt += qres.size();
	}
	state.SetLabel(FormatString("RPR: %.1f", cnt / double(state.iterations())));
}

string FullText::CreatePhrase() {
	size_t wordCnt = 100;
	string result;
	result.reserve(wordCnt * 3);

	for (size_t i = 0; i < wordCnt; i++) {
		result += words_.at(random<size_t>(0, words_.size() - 1));
		if (i < wordCnt - 1) result += " ";
	}

	return result;
}

string FullText::MakePrefixWord() {
	auto word = GetRandomUTF16WordByLength(4);

	auto pos = random<wstring::size_type>(2, word.length() - 2);
	word.erase(pos, word.length() - pos);
	word += L"*";

	return reindexer::utf16_to_utf8(word);
}

string FullText::MakeSuffixWord() {
	auto word = GetRandomUTF16WordByLength(4);
	auto cnt = random<wstring::size_type>(0, word.length() / 2);
	word.erase(0, cnt);
	word = L"*" + word;
	return utf16_to_utf8(word);
}

string FullText::MakeTypoWord() {
	static const wstring wchars =
		L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZабвгдежзийклмнопрстуфхцчшщъыьэюяАБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ";
	auto word = GetRandomUTF16WordByLength(2);
	word[random<wstring::size_type>(0, word.length() - 1)] = wchars.at(random<wstring::size_type>(0, wchars.size() - 1));
	word += L"~";
	return utf16_to_utf8(word);
}

wstring FullText::GetRandomUTF16WordByLength(size_t minLen) {
	wstring word;
	for (; word.length() < minLen;) {
		auto& w = words_.at(random<size_t>(0, words_.size() - 1));
		word = reindexer::utf8_to_utf16(w);
	}
	return word;
}

vector<std::string> FullText::GetRandomCountries(size_t cnt) {
	vector<string> result;
	for (auto i = cnt; i > 0; i--) {
		result.emplace_back(countries_.at(random<size_t>(0, countries_.size() - 1)));
	}
	return result;
}
