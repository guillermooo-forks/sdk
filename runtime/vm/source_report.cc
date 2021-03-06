// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/source_report.h"

#include "vm/compiler.h"
#include "vm/object.h"
#include "vm/object_store.h"

namespace dart {

SourceReport::SourceReport(ReportKind report_kind, CompileMode compile_mode)
    : report_kind_(report_kind),
      compile_mode_(compile_mode),
      thread_(NULL),
      script_(NULL),
      start_pos_(-1),
      end_pos_(-1),
      next_script_index_(0) {
}


void SourceReport::Init(Thread* thread,
                        const Script* script,
                        intptr_t start_pos,
                        intptr_t end_pos) {
  thread_ = thread;
  script_ = script;
  start_pos_ = start_pos;
  end_pos_ = end_pos;
  script_table_entries_.Clear();
  script_table_.Clear();
  next_script_index_ = 0;
}


bool SourceReport::ShouldSkipFunction(const Function& func) {
  if (script_ != NULL && !script_->IsNull()) {
    if (func.script() != script_->raw()) {
      // The function is from the wrong script.
      return true;
    }
    if (((start_pos_ > 0) && (func.end_token_pos() < start_pos_)) ||
        ((end_pos_ > 0) && (func.token_pos() > end_pos_))) {
      // The function does not intersect with the requested token range.
      return true;
    }
  }

  switch (func.kind()) {
    case RawFunction::kRegularFunction:
    case RawFunction::kClosureFunction:
    case RawFunction::kGetterFunction:
    case RawFunction::kSetterFunction:
    case RawFunction::kConstructor:
      break;
    default:
      return true;
  }
  if (func.is_abstract() ||
      func.IsImplicitConstructor() ||
      func.IsRedirectingFactory()) {
    return true;
  }
  if (func.IsNonImplicitClosureFunction() &&
      (func.context_scope() == ContextScope::null())) {
    // TODO(iposva): This can arise if we attempt to compile an inner function
    // before we have compiled its enclosing function or if the enclosing
    // function failed to compile.
    return true;
  }
  return false;
}


intptr_t SourceReport::GetScriptIndex(const Script& script) {
  const String& url = String::Handle(zone(), script.url());
  ScriptTableEntry* pair = script_table_.Lookup(&url);
  if (pair != NULL) {
    return pair->index;
  }

  ScriptTableEntry tmp;
  tmp.key = &url;
  tmp.index = next_script_index_++;
  tmp.script = &script;
  script_table_entries_.Add(tmp);
  script_table_.Insert(&(script_table_entries_.Last()));
  return tmp.index;
}


bool SourceReport::ScriptIsLoadedByLibrary(const Script& script,
                                           const Library& lib) {
  const Array& scripts = Array::Handle(zone(), lib.LoadedScripts());
  for (intptr_t j = 0; j < scripts.Length(); j++) {
    if (scripts.At(j) == script.raw()) {
      return true;
    }
  }
  return false;
}


void SourceReport::PrintCallSitesData(JSONObject* jsobj,
                                      const Function& func,
                                      const Code& code) {
  const intptr_t begin_pos = func.token_pos();
  const intptr_t end_pos = func.end_token_pos();

  ZoneGrowableArray<const ICData*>* ic_data_array =
      new(zone()) ZoneGrowableArray<const ICData*>();
  func.RestoreICDataMap(ic_data_array, false /* clone descriptors */);
  const PcDescriptors& descriptors = PcDescriptors::Handle(
      zone(), code.pc_descriptors());

  JSONArray sites(jsobj, "callSites");

  PcDescriptors::Iterator iter(
      descriptors,
      RawPcDescriptors::kIcCall | RawPcDescriptors::kUnoptStaticCall);
  while (iter.MoveNext()) {
    HANDLESCOPE(thread());
    const ICData* ic_data = (*ic_data_array)[iter.DeoptId()];
    if (!ic_data->IsNull()) {
      const intptr_t token_pos = iter.TokenPos();
      if ((token_pos < begin_pos) || (token_pos > end_pos)) {
        // Does not correspond to a valid source position.
        continue;
      }
      bool is_static_call = iter.Kind() == RawPcDescriptors::kUnoptStaticCall;
      ic_data->PrintToJSONArrayNew(sites, token_pos, is_static_call);
    }
  }
}

void SourceReport::PrintCoverageData(JSONObject* jsobj,
                                     const Function& func,
                                     const Code& code) {
  const intptr_t begin_pos = func.token_pos();
  const intptr_t end_pos = func.end_token_pos();

  ZoneGrowableArray<const ICData*>* ic_data_array =
      new(zone()) ZoneGrowableArray<const ICData*>();
  func.RestoreICDataMap(ic_data_array, false /* clone descriptors */);
  const PcDescriptors& descriptors = PcDescriptors::Handle(
      zone(), code.pc_descriptors());

  const int kCoverageNone = 0;
  const int kCoverageMiss = 1;
  const int kCoverageHit = 2;

  intptr_t func_length = (end_pos - begin_pos) + 1;
  GrowableArray<char> coverage(func_length);
  coverage.SetLength(func_length);
  for (int i = 0; i < func_length; i++) {
    coverage[i] = kCoverageNone;
  }

  PcDescriptors::Iterator iter(
      descriptors,
      RawPcDescriptors::kIcCall | RawPcDescriptors::kUnoptStaticCall);
  while (iter.MoveNext()) {
    HANDLESCOPE(thread());
    const ICData* ic_data = (*ic_data_array)[iter.DeoptId()];
    if (!ic_data->IsNull()) {
      const intptr_t token_pos = iter.TokenPos();
      if ((token_pos < begin_pos) || (token_pos > end_pos)) {
        // Does not correspond to a valid source position.
        continue;
      }
      intptr_t count = ic_data->AggregateCount();
      intptr_t token_offset = token_pos - begin_pos;
      if (count > 0) {
        coverage[token_offset] = kCoverageHit;
      } else {
        if (coverage[token_offset] == kCoverageNone) {
          coverage[token_offset] = kCoverageMiss;
        }
      }
    }
  }

  JSONObject cov(jsobj, "coverage");
  {
    JSONArray hits(&cov, "hits");
    for (int i = 0; i < func_length; i++) {
      if (coverage[i] == kCoverageHit) {
        hits.AddValue(begin_pos + i);  // Add the token position of the hit.
      }
    }
  }
  {
    JSONArray misses(&cov, "misses");
    for (int i = 0; i < func_length; i++) {
      if (coverage[i] == kCoverageMiss) {
        misses.AddValue(begin_pos + i);  // Add the token position of the miss.
      }
    }
  }
}


void SourceReport::PrintScriptTable(JSONArray* scripts) {
  for (int i = 0; i < script_table_entries_.length(); i++) {
    const Script* script = script_table_entries_[i].script;
    scripts->AddValue(*script);
  }
}


void SourceReport::VisitFunction(JSONArray* jsarr, const Function& func) {
  if (ShouldSkipFunction(func)) {
    return;
  }

  const Script& script = Script::Handle(zone(), func.script());
  const intptr_t begin_pos = func.token_pos();
  const intptr_t end_pos = func.end_token_pos();

  Code& code = Code::Handle(zone(), func.unoptimized_code());
  if (code.IsNull()) {
    if (func.HasCode() || (compile_mode_ == kForceCompile)) {
      if (Compiler::EnsureUnoptimizedCode(thread(), func) != Error::null()) {
        // Ignore the error and this function entirely.
        return;
      }
      code = func.unoptimized_code();
    } else {
      // This function has not been compiled yet.
      JSONObject range(jsarr);
      range.AddProperty("scriptIndex", GetScriptIndex(script));
      range.AddProperty("startPos", begin_pos);
      range.AddProperty("endPos", end_pos);
      range.AddProperty("compiled", false);
      return;
    }
  }
  ASSERT(!code.IsNull());

  JSONObject range(jsarr);
  range.AddProperty("scriptIndex", GetScriptIndex(script));
  range.AddProperty("startPos", begin_pos);
  range.AddProperty("endPos", end_pos);
  range.AddProperty("compiled", true);

  if (report_kind_ == kCallSites) {
    PrintCallSitesData(&range, func, code);
  } else if (report_kind_ == kCoverage) {
    PrintCoverageData(&range, func, code);
  }
}


void SourceReport::VisitLibrary(JSONArray* jsarr, const Library& lib) {
  Class& cls = Class::Handle(zone());
  Array& functions = Array::Handle(zone());
  Function& func = Function::Handle(zone());
  ClassDictionaryIterator it(lib, ClassDictionaryIterator::kIteratePrivate);
  while (it.HasNext()) {
    cls = it.GetNextClass();
    functions = cls.functions();
    for (int i = 0; i < functions.Length(); i++) {
      func ^= functions.At(i);
      VisitFunction(jsarr, func);
    }
  }
}


void SourceReport::VisitClosures(JSONArray* jsarr) {
  const GrowableObjectArray& closures = GrowableObjectArray::Handle(
      thread()->isolate()->object_store()->closure_functions());

  // We need to keep rechecking the length of the closures array, as handling
  // a closure potentially adds new entries to the end.
  Function& func = Function::Handle(zone());
  for (int i = 0; i < closures.Length(); i++) {
    func ^= closures.At(i);
    VisitFunction(jsarr, func);
  }
}


void SourceReport::PrintJSON(JSONStream* js,
                             const Script& script,
                             intptr_t start_pos, intptr_t end_pos) {
  Init(Thread::Current(), &script, start_pos, end_pos);

  JSONObject report(js);
  report.AddProperty("type", "SourceReport");
  {
    JSONArray ranges(&report, "ranges");

    const GrowableObjectArray& libs = GrowableObjectArray::Handle(
        zone(), thread()->isolate()->object_store()->libraries());

    // We only visit the libraries which actually load the specified script.
    Library& lib = Library::Handle(zone());
    for (int i = 0; i < libs.Length(); i++) {
      lib ^= libs.At(i);
      if (script.IsNull() || ScriptIsLoadedByLibrary(script, lib)) {
        VisitLibrary(&ranges, lib);
      }
    }

    // Visit all closures for this isolate.
    VisitClosures(&ranges);
  }

  // Print the script table.
  JSONArray scripts(&report, "scripts");
  PrintScriptTable(&scripts);
}


}  // namespace dart
