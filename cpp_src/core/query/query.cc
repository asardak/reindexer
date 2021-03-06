
#include "core/query/query.h"
#include "core/query/dslencoder.h"
#include "core/query/dslparsetools.h"
#include "core/type_consts.h"
#include "estl/tokenizer.h"
#include "gason/gason.h"
#include "tools/errors.h"
#include "tools/serializer.h"

namespace reindexer {

Query::Query(const string &__namespace, unsigned _start, unsigned _count, CalcTotalMode _calcTotal)
	: _namespace(__namespace), calcTotal(_calcTotal), start(_start), count(_count) {}

bool Query::operator==(const Query &obj) const {
	if (!QueryWhere::operator==(obj)) return false;

	if (nextOp_ != obj.nextOp_) return false;
	if (_namespace != obj._namespace) return false;
	if (sortBy != obj.sortBy) return false;
	if (sortDirDesc != obj.sortDirDesc) return false;
	if (calcTotal != obj.calcTotal) return false;
	if (describe != obj.describe) return false;
	if (start != obj.start) return false;
	if (count != obj.count) return false;
	if (debugLevel != obj.debugLevel) return false;
	if (joinType != obj.joinType) return false;
	if (forcedSortOrder != obj.forcedSortOrder) return false;
	if (namespacesNames_ != obj.namespacesNames_) return false;

	if (selectFilter_ != obj.selectFilter_) return false;
	if (selectFunctions_ != obj.selectFunctions_) return false;
	if (joinQueries_ != obj.joinQueries_) return false;
	if (mergeQueries_ != obj.mergeQueries_) return false;

	return true;
}

int Query::Parse(const string &q) {
	tokenizer parser(q);
	return Parse(parser);
}

Error Query::ParseJson(const string &dsl) {
	try {
		parseJson(dsl);
	} catch (const Error &e) {
		return e;
	}
	return Error();
}

void Query::parseJson(const string &dsl) {
	JsonAllocator allocator;
	JsonValue root;
	char *endptr = nullptr;
	char *src = const_cast<char *>(dsl.data());

	auto error = jsonParse(src, &endptr, &root, allocator);
	if (error != JSON_OK) {
		throw Error(errParseJson, "Could not parse JSON-query: %s at %zd", jsonStrError(error), endptr - src);
	}
	dsl::parse(root, *this);
}

void Query::deserialize(Serializer &ser) {
	while (!ser.Eof()) {
		QueryEntry qe;
		QueryJoinEntry qje;

		int qtype = ser.GetVarUint();
		switch (qtype) {
			case QueryCondition: {
				qe.index = ser.GetVString().ToString();
				qe.op = OpType(ser.GetVarUint());
				qe.condition = CondType(ser.GetVarUint());
				int count = ser.GetVarUint();
				qe.values.reserve(count);
				while (count--) qe.values.push_back(ser.GetValue());
				entries.push_back(std::move(qe));
				break;
			}
			case QueryAggregation:
				aggregations_.push_back({ser.GetVString().ToString(), AggType(ser.GetVarUint())});
				break;
			case QueryDistinct:
				qe.index = ser.GetVString().ToString();
				qe.distinct = true;
				qe.condition = CondAny;
				entries.push_back(std::move(qe));
				break;
			case QuerySortIndex: {
				sortBy = ser.GetVString().ToString();
				sortDirDesc = bool(ser.GetVarUint());
				int count = ser.GetVarUint();
				forcedSortOrder.reserve(count);
				while (count--) forcedSortOrder.push_back(ser.GetValue());
				break;
			}
			case QueryJoinOn:
				qje.op_ = OpType(ser.GetVarUint());
				qje.condition_ = CondType(ser.GetVarUint());
				qje.index_ = ser.GetVString().ToString();
				qje.joinIndex_ = ser.GetVString().ToString();
				joinEntries_.push_back(std::move(qje));
				break;
			case QueryDebugLevel:
				debugLevel = ser.GetVarUint();
				break;
			case QueryLimit:
				count = ser.GetVarUint();
				break;
			case QueryOffset:
				start = ser.GetVarUint();
				break;
			case QueryReqTotal:
				calcTotal = CalcTotalMode(ser.GetVarUint());
				break;
			case QuerySelectFilter:
				selectFilter_.push_back(ser.GetVString().ToString());
				break;
			case QuerySelectFunction:
				selectFunctions_.push_back(ser.GetVString().ToString());
				break;
			case QueryEnd:
				return;
		}
	}
}

int Query::Parse(tokenizer &parser) {
	token tok = parser.next_token();

	if (tok.text == "describe") {
		describeParse(parser);
	} else if (tok.text == "select") {
		selectParse(parser);
	} else {
		throw Error(errParams, "Syntax error at or near '%s', %s", tok.text.c_str(), parser.where().c_str());
	}
	tok = parser.next_token();
	if (tok.text == ";") {
		tok = parser.next_token();
	}
	parser.skip_space();
	if (tok.text != "" || !parser.end()) throw Error(errParseSQL, "Unexpected '%s' in query, %s", tok.text.c_str(), parser.where().c_str());

	return 0;
}

int Query::selectParse(tokenizer &parser) {
	// Get filter
	token tok;
	while (!parser.end()) {
		auto nameWithCase = parser.peek_token(false).text;
		auto name = parser.next_token().text;
		tok = parser.peek_token();
		if (tok.text == "(") {
			parser.next_token();
			tok = parser.next_token();
			if (name == "avg") {
				aggregations_.push_back({tok.text, AggAvg});
			} else if (name == "sum") {
				aggregations_.push_back({tok.text, AggSum});
			} else if (name == "count") {
				calcTotal = ModeAccurateTotal;
				count = 0;
			} else {
				throw Error(errParams, "Unknown function name SQL - %s, %s", name.c_str(), parser.where().c_str());
			}
			tok = parser.next_token();
			if (tok.text != ")") {
				throw Error(errParams, "Expected ')', but found %s, %s", tok.text.c_str(), parser.where().c_str());
			}
			tok = parser.peek_token();

		} else if (name != "*") {
			selectFilter_.push_back(nameWithCase);
			count = INT_MAX;
		} else if (name == "*") {
			count = INT_MAX;
		}
		if (tok.text != ",") break;
		tok = parser.next_token();
	}

	if (parser.next_token().text != "from")
		throw Error(errParams, "Expected 'FROM', but found '%s' in query, %s", tok.text.c_str(), parser.where().c_str());

	_namespace = parser.next_token().text;
	parser.skip_space();

	while (!parser.end()) {
		tok = parser.peek_token();
		if (tok.text == "where") {
			parser.next_token();
			ParseWhere(parser);
		} else if (tok.text == "limit") {
			parser.next_token();
			tok = parser.next_token();
			if (tok.type != TokenNumber)
				throw Error(errParseSQL, "Expected number, but found '%s' in query, %s", tok.text.c_str(), parser.where().c_str());
			count = stoi(tok.text);
		} else if (tok.text == "offset") {
			parser.next_token();
			tok = parser.next_token();
			if (tok.type != TokenNumber)
				throw Error(errParseSQL, "Expected number, but found '%s' in query, %s", tok.text.c_str(), parser.where().c_str());
			start = stoi(tok.text);
		} else if (tok.text == "order") {
			parser.next_token();
			// Just skip token (BY)
			parser.next_token();
			auto nameWithCase = parser.peek_token().text;
			tok = parser.next_token(false);
			if (tok.type != TokenName)
				throw Error(errParseSQL, "Expected name, but found '%s' in query, %s", tok.text.c_str(), parser.where().c_str());
			sortBy = tok.text;
			tok = parser.peek_token();
			if (tok.text == "(" && nameWithCase == "field") {
				parser.next_token();
				tok = parser.next_token(false);
				if (tok.type != TokenName)
					throw Error(errParseSQL, "Expected name, but found '%s' in query, %s", tok.text.c_str(), parser.where().c_str());
				sortBy = tok.text;
				for (;;) {
					tok = parser.next_token();
					if (tok.text == ")") break;
					if (tok.text != ",")
						throw Error(errParseSQL, "Expected ')' or ',', but found '%s' in query, %s", tok.text.c_str(),
									parser.where().c_str());
					tok = parser.next_token();
					if (tok.type != TokenNumber && tok.type != TokenString)
						throw Error(errParseSQL, "Expected parameter, but found '%s' in query, %s", tok.text.c_str(),
									parser.where().c_str());
					forcedSortOrder.push_back(KeyValue(tok.text));
				}
				tok = parser.peek_token();
			}

			if (tok.text == "asc" || tok.text == "desc") {
				sortDirDesc = bool(tok.text == "desc");
				parser.next_token();
			}
		} else if (tok.text == "join") {
			parser.next_token();
			parseJoin(JoinType::LeftJoin, parser);
		} else if (tok.text == "left") {
			parser.next_token();
			if (parser.next_token().text != "join") {
				throw Error(errParseSQL, "Expected JOIN, but found '%s' in query, %s", tok.text.c_str(), parser.where().c_str());
			}
			parseJoin(JoinType::LeftJoin, parser);
		} else if (tok.text == "inner") {
			parser.next_token();
			if (parser.next_token().text != "join") {
				throw Error(errParseSQL, "Expected JOIN, but found '%s' in query, %s", tok.text.c_str(), parser.where().c_str());
			}
			auto jtype = nextOp_ == OpOr ? JoinType::OrInnerJoin : JoinType::InnerJoin;
			nextOp_ = OpAnd;
			parseJoin(jtype, parser);
		} else if (tok.text == "merge") {
			parser.next_token();
			parseMerge(parser);
		} else if (tok.text == "or") {
			parser.next_token();
			nextOp_ = OpOr;
		} else {
			break;
		}
	}
	return 0;
}

int Query::describeParse(tokenizer &parser) {
	// Get namespaces
	token tok = parser.next_token(false);
	parser.skip_space();

	if (tok.text != "*") {
		for (;;) {
			namespacesNames_.push_back(tok.text);
			tok = parser.peek_token();
			if (tok.text != ",") {
				token nextTok = parser.next_token(false);
				if (nextTok.text.length()) {
					throw Error(errParseSQL, "Unexpected '%s' in query, %s", tok.text.c_str(), parser.where().c_str());
				}
				break;
			}

			parser.next_token();
			tok = parser.next_token(false);
			if (parser.end()) {
				namespacesNames_.push_back(tok.text);
				break;
			}
		}
	}
	describe = true;

	return 0;
}

void Query::parseJoin(JoinType type, tokenizer &parser) {
	Query jquery;
	auto tok = parser.next_token();
	if (tok.text == "(") {
		tok = parser.next_token();
		if (tok.text != "select") {
			throw Error(errParseSQL, "Expected 'SELECT', but found %s, %s", tok.text.c_str(), parser.where().c_str());
		}
		jquery.selectParse(parser);
		tok = parser.next_token();
		if (tok.text != ")") {
			throw Error(errParseSQL, "Expected ')', but found %s, %s", tok.text.c_str(), parser.where().c_str());
		}
	} else {
		jquery._namespace = tok.text;
	}
	jquery.joinType = type;
	jquery.parseJoinEntries(parser, _namespace);

	joinQueries_.push_back(std::move(jquery));
}

void Query::parseMerge(tokenizer &parser) {
	Query mquery;
	auto tok = parser.next_token();
	if (tok.text == "(") {
		tok = parser.next_token();
		if (tok.text != "select") {
			throw Error(errParseSQL, "Expected 'SELECT', but found %s, %s", tok.text.c_str(), parser.where().c_str());
		}
		mquery.selectParse(parser);
		tok = parser.next_token();
		if (tok.text != ")") {
			throw Error(errParseSQL, "Expected ')', but found %s, %s", tok.text.c_str(), parser.where().c_str());
		}
	}
	mquery.joinType = JoinType::Merge;

	mergeQueries_.push_back(std::move(mquery));
}

// parse [table.]field
// return field
string parseDotStr(tokenizer &parser, string &str1) {
	auto tok = parser.next_token();
	if (tok.type != TokenName && tok.type != TokenString) {
		throw Error(errParseSQL, "Expected name, but found %s, %s", tok.text.c_str(), parser.where().c_str());
	}
	if (parser.peek_token().text != ".") {
		return tok.text;
	}
	parser.next_token();
	str1 = tok.text;

	tok = parser.next_token();
	if (tok.type != TokenName && tok.type != TokenString) {
		throw Error(errParseSQL, "Expected name, but found %s, %s", tok.text.c_str(), parser.where().c_str());
	}
	return tok.text;
}

void Query::parseJoinEntries(tokenizer &parser, const string &mainNs) {
	parser.skip_space();
	QueryJoinEntry je;
	auto tok = parser.next_token();
	if (tok.text != "on") {
		throw Error(errParseSQL, "Expected 'ON', but found %s, %s", tok.text.c_str(), parser.where().c_str());
	}

	tok = parser.peek_token();

	bool braces = tok.text == "(";
	if (braces) parser.next_token();

	while (!parser.end()) {
		auto tok = parser.peek_token();
		if (tok.text == "or") {
			nextOp_ = OpOr;
			parser.next_token();
			tok = parser.peek_token();
		} else if (tok.text == "and") {
			nextOp_ = OpAnd;
			parser.next_token();
			tok = parser.peek_token();
		}

		if (braces && tok.text == ")") {
			parser.next_token();
			return;
		}

		string ns1 = mainNs, ns2 = _namespace;
		string idx1 = parseDotStr(parser, ns1);
		je.condition_ = getCondType(parser.next_token().text);
		string idx2 = parseDotStr(parser, ns2);

		if (ns1 == mainNs && ns2 == _namespace) {
			je.index_ = idx1;
			je.joinIndex_ = idx2;
		} else if (ns2 == mainNs && ns1 == _namespace) {
			je.index_ = idx2;
			je.joinIndex_ = idx1;
		} else {
			throw Error(errParseSQL, "Unexpected tables with ON statement: ('%s' and '%s') but expected ('%s' and '%s'), %s", ns1.c_str(),
						ns2.c_str(), mainNs.c_str(), _namespace.c_str(), parser.where().c_str());
		}

		je.op_ = nextOp_;
		nextOp_ = OpAnd;
		joinEntries_.push_back(std::move(je));
		if (!braces) {
			return;
		}
	}
}

void Query::Serialize(WrSerializer &ser, uint8_t mode) const {
	ser.PutVString(_namespace);
	for (auto &qe : entries) {
		qe.distinct ? ser.PutVarUint(QueryDistinct) : ser.PutVarUint(QueryCondition);
		ser.PutVString(qe.index);
		if (qe.distinct) continue;
		ser.PutVarUint(qe.op);
		ser.PutVarUint(qe.condition);
		ser.PutVarUint(qe.values.size());
		for (auto &kv : qe.values) ser.PutValue(kv);
	}

	for (auto &agg : aggregations_) {
		ser.PutVarUint(QueryAggregation);
		ser.PutVString(agg.index_);
		ser.PutVarUint(agg.type_);
	}

	if (!sortBy.empty()) {
		ser.PutVarUint(QuerySortIndex);
		ser.PutVString(sortBy);
		ser.PutVarUint(sortDirDesc);
		int cnt = forcedSortOrder.size();
		ser.PutVarUint(cnt);
		for (auto &kv : forcedSortOrder) ser.PutValue(kv);
	}

	for (auto &qje : joinEntries_) {
		ser.PutVarUint(QueryJoinOn);
		ser.PutVarUint(qje.op_);
		ser.PutVarUint(qje.condition_);
		ser.PutVString(qje.index_);
		ser.PutVString(qje.joinIndex_);
	}

	ser.PutVarUint(QueryDebugLevel);
	ser.PutVarUint(debugLevel);

	if (!(mode & SkipLimitOffset)) {
		if (count) {
			ser.PutVarUint(QueryLimit);
			ser.PutVarUint(count);
		}
		if (start) {
			ser.PutVarUint(QueryOffset);
			ser.PutVarUint(start);
		}
	}

	if (calcTotal) {
		ser.PutVarUint(QueryReqTotal);
		ser.PutVarUint(calcTotal);
	}

	for (auto &sf : selectFilter_) {
		ser.PutVarUint(QuerySelectFilter);
		ser.PutVString(sf);
	}

	ser.PutVarUint(QueryEnd);  // finita la commedia... of root query

	if (!(mode & SkipJoinQueries)) {
		for (auto &jq : joinQueries_) {
			ser.PutVarUint(static_cast<int>(jq.joinType));
			jq.Serialize(ser);
		}
	}

	if (!(mode & SkipMergeQueries)) {
		for (auto &mq : mergeQueries_) {
			ser.PutVarUint(static_cast<int>(mq.joinType));
			mq.Serialize(ser);
		}
	}
}

void Query::Deserialize(Serializer &ser) {
	_namespace = ser.GetVString().ToString();
	deserialize(ser);

	while (!ser.Eof()) {
		auto joinType = JoinType(ser.GetVarUint());
		Query q1(ser.GetVString().ToString());
		q1.joinType = joinType;
		q1.deserialize(ser);
		q1.debugLevel = debugLevel;
		if (joinType == JoinType::Merge) {
			mergeQueries_.emplace_back(std::move(q1));
		} else {
			joinQueries_.emplace_back(std::move(q1));
		}
	}
}

string Query::GetJSON() { return dsl::toDsl(*this); }

const char *Query::JoinTypeName(JoinType type) {
	switch (type) {
		case JoinType::InnerJoin:
			return "INNER JOIN";
		case JoinType::OrInnerJoin:
			return "OR INNER JOIN";
		case JoinType::LeftJoin:
			return "LEFT JOIN";
		case JoinType::Merge:
			return "MERGE";
		default:
			return "<unknown>";
	}
}

string Query::dumpJoined() const {
	extern const char *condNames[];
	string ret;
	for (auto &je : joinQueries_) {
		ret += string(" ") + JoinTypeName(je.joinType);

		if (je.entries.empty() && je.count == INT_MAX) {
			ret += " " + je._namespace + " ON ";
		} else {
			ret += " (" + je.Dump() + ") ON ";
		}
		if (je.joinEntries_.size() != 1) ret += "(";
		for (auto &e : je.joinEntries_) {
			if (&e != &*je.joinEntries_.begin()) {
				ret += (e.op_ == OpOr) ? " OR " : " AND ";
			}
			ret += je._namespace + "." + e.joinIndex_ + " " + condNames[e.condition_] + " " + _namespace + "." + e.index_;
		}
		if (je.joinEntries_.size() != 1) ret += ")";
	}

	return ret;
}

string Query::dumpMerged() const {
	string ret;
	for (auto &me : mergeQueries_) {
		ret += " " + string(JoinTypeName(me.joinType)) + "( " + me.Dump() + ")";
	}
	return ret;
}

string Query::dumpOrderBy() const {
	string ret;
	if (sortBy.empty()) {
		return ret;
	}
	ret = " ORDER BY ";
	if (forcedSortOrder.empty()) {
		ret += sortBy;
	} else {
		ret += "FIELD(" + sortBy;
		for (auto &v : forcedSortOrder) {
			ret += ", '" + v.As<string>() + "'";
		}
		ret += ")";
	}

	return ret + (sortDirDesc ? " DESC" : "");
}

string Query::Dump() const {
	string lim, filt;
	if (start != 0) lim += " OFFSET " + std::to_string(start);
	if (count != UINT_MAX) lim += " LIMIT " + std::to_string(count);

	if (aggregations_.size()) {
		for (auto &a : aggregations_) {
			if (&a != &*aggregations_.begin()) filt += ",";
			switch (a.type_) {
				case AggAvg:
					filt += "AVG(";
					break;
				case AggSum:
					filt += "SUM(";
					break;
				default:
					filt += "<?> (";
					break;
			}
			filt += a.index_ + ")";
		}
	} else if (selectFilter_.size()) {
		for (auto &f : selectFilter_) {
			if (&f != &*selectFilter_.begin()) filt += ",";
			filt += f;
		}
	} else
		filt = "*";
	if (calcTotal) filt += ", COUNT(*)";

	string buf = "SELECT " + filt + " FROM " + _namespace + QueryWhere::toString() + dumpJoined() + dumpMerged() + dumpOrderBy() + lim;
	return buf;
}

}  // namespace reindexer
