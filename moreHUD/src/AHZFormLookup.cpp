﻿#include "PCH.h"
#include "AHZFormLookup.h"


class CAHZGetScriptVariableFunctor : public RE::BSScript::IForEachScriptObjectFunctor
{
public:
    CAHZGetScriptVariableFunctor(
        string a_varName)
    {
        m_result.SetNone();

        string prefix = "::";
        string suffix = "_var";

        // Properties omit the prefix and sufix, but we are looking at variables
        m_variable = prefix + a_varName + suffix;
    }

    virtual ~CAHZGetScriptVariableFunctor() = default;

   virtual inline bool Visit(RE::BSScript::ScriptObjectMessage* script, [[maybe_unused ]] void* arg2)
   {
       auto classInfo = script->typeInfo;
       if (!classInfo) {
           return true;
       }

       if (classInfo->variableCount) {
           return true;
       }

       auto iter = classInfo->GetVariableIter();
       if (iter) {
           for (std::uint32_t i = 0; i < classInfo->GetTotalNumVariables(); ++i) {
               auto& prop = iter[i];
               if (std::string(prop.name.c_str()) == m_variable) {
                   auto vm = RE::SkyrimVM::GetSingleton()->impl;
                   RE::BSTSmartPointer<RE::BSScript::Object> boundObject;
                   vm->FindBoundObject(script->handle, classInfo->name.data(), boundObject);
                   
                   auto found = vm->GetVariableValue(boundObject, i, m_result);

                   if (found) {
                       logger::trace("found variable: {}, on script: {}", prop.name.c_str(), classInfo->name);
                   }

                   // Found returns false to stop the visitor
                   return !found;
               }
           }
       }
       return true;
   }

   auto GetScriptVariable() -> RE::BSScript::Variable* { return (m_result.IsNoneObject() || m_result.IsNoneArray()) ? nullptr : &m_result; }
   

private:
   string	m_variable;
   RE::BSScript::Variable m_result;
};

CAHZFormLookup& CAHZFormLookup::Instance() {
   static CAHZFormLookup theInstance;
   return theInstance;
}

RE::TESForm * CAHZFormLookup::GetTESForm(RE::TESObjectREFR * targetReference)
{
	RE::TESForm * lutForm = nullptr;
	if ((lutForm = GetFormFromLookup(targetReference)) != nullptr)
	{
		return lutForm;
	}
	else if (targetReference->GetBaseObject() && targetReference->GetBaseObject()->formType == RE::FormType::Activator)
	{
		return GetAttachedForm(targetReference);
	} 
    else if (targetReference->GetBaseObject() && targetReference->GetBaseObject()->formType == RE::FormType::Projectile)
	{
		auto pProjectile = (DYNAMIC_CAST(targetReference, RE::TESObjectREFR, RE::Projectile));

		if (pProjectile) {
			AHZProjectile *a = reinterpret_cast<AHZProjectile*>(pProjectile);
			if (a && a->sourceAmmo)
				return a->sourceAmmo;
			else
				return targetReference;
		}
		else
			return targetReference;
	}
	else
	{
		return targetReference;
	}
}

RE::TESForm * CAHZFormLookup::GetFormFromLookup(RE::TESObjectREFR * targetRef)
{
	if (!targetRef || !targetRef->GetBaseObject())
		return nullptr;

	if (m_LUT.find(targetRef->GetBaseObject()->formID) != m_LUT.end())
	{
        uint32_t formID = m_LUT.find(targetRef->GetBaseObject()->formID)->second;
		auto form = RE::TESForm::LookupByID(formID);
		return form;
	}
	return NULL;
}

void CAHZFormLookup::AddScriptVarable(string vmVariableName)
{
   if (find(m_scriptVMVariables.begin(), m_scriptVMVariables.end(), vmVariableName) == m_scriptVMVariables.end())
   {
      m_scriptVMVariables.push_back(vmVariableName);
   }
}

void CAHZFormLookup::AddFormID(string baseFormModName, uint32_t baseFormID, string targetFormModName, uint32_t targetFormID)
{
    auto dataHandler = RE::TESDataHandler::GetSingleton();

    // Normalize to the raw formIDs
    auto isBaseLight = dataHandler->GetLoadedLightModIndex(baseFormModName).has_value();
    baseFormID = isBaseLight ? baseFormID & 0x00000FFF : baseFormID & 0x00FFFFFF;
    auto isTargetLight = dataHandler->GetLoadedLightModIndex(targetFormModName).has_value();
    targetFormID = isBaseLight ? targetFormID & 0x00000FFF : targetFormID & 0x00FFFFFF;

    auto baseForm = dataHandler->LookupForm(baseFormID, baseFormModName);

    if (!baseForm) {
        return;
    }

    auto targetForm = dataHandler->LookupForm(targetFormID, targetFormModName);
    if (!targetForm) {
        return;
    }

    // Load into map if the entry does not already exist
    if (m_LUT.find(baseForm->formID) == m_LUT.end()) {
        m_LUT[baseForm->formID] = targetForm->formID;
        logger::trace("ACTI BASE ID:{:#08x}, ACTI Targ ID:{:#08x}", baseForm->formID, targetForm->formID);
    }
}

RE::TESForm* CAHZFormLookup::GetAttachedForm(RE::TESObjectREFR* form)
{
    vector<string>::iterator p;

    if (!form) {
        return nullptr;
    }

    if (!form->GetBaseObject()) {
        return NULL;
    }

    if (form->GetBaseObject()->formType != RE::FormType::Activator) {
        return nullptr;
    }

    for (p = m_scriptVMVariables.begin(); p != m_scriptVMVariables.end(); p++) {
        //_MESSAGE("GetAttachedForm");
        RE::TESForm* attachedForm = NULL;
        if ((attachedForm = GetAttachedForm(form, *p)) != NULL) {
            if (attachedForm->formType == RE::FormType::LeveledItem) {
                auto lvli = DYNAMIC_CAST(attachedForm, RE::TESForm, RE::TESLevItem);

                // Get the first form and see if it is an ingredient
                if (lvli && lvli->entries.size() > 0) {
                    auto itemform = lvli->entries[0].form;
                    return itemform;
                }
            } else if (attachedForm->formType == RE::FormType::FormList) {
                auto* lvli = DYNAMIC_CAST(attachedForm, RE::TESForm, RE::BGSListForm);

                // Get the first form and see if it is an ingredient
                if (lvli && lvli->forms.size() > 0) {
                    auto itemform = lvli->forms[0];
                    return itemform;
                }
            } else {
                return attachedForm;
            }
        }
    }

    return nullptr;
}

auto CAHZFormLookup::GetScriptVariable(RE::TESForm* a_form, const char* a_scriptName, const char* a_scriptVariable) -> RE::BSScript::Variable const
{
    RE::BSScript::Variable var;
    if (a_form == nullptr) {
        return var;
    }

    auto variableName = std::string(a_scriptVariable);
    variableName.insert(0, "::");
    variableName.append("_var");
    auto                                      vm = RE::SkyrimVM::GetSingleton();
    auto                                      vmImpl = vm->impl;
    auto                                      handlePolicy = vmImpl.get()->GetObjectHandlePolicy();
    auto                                      handle = handlePolicy->GetHandleForObject(a_form->GetFormType(), a_form);
    RE::BSTSmartPointer<RE::BSScript::Object> result;

    vmImpl->FindBoundObject(handle, a_scriptName, result);

    if (!result.get()) {
        return var;
    }

    RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> info;
    vmImpl->GetScriptObjectType(a_scriptName, info);
    auto iter = info->GetVariableIter();
    if (iter) {
        for (std::uint32_t i = 0; i < info->GetTotalNumVariables(); ++i) {
            auto& prop = iter[i];
            if (std::string(prop.name.c_str()) == variableName) {
                vmImpl->GetVariableValue(result, i, var);
                return var;
            }
        }
    }

    return var;
}

RE::TESForm* CAHZFormLookup::GetAttachedForm(RE::TESObjectREFR *form, string variableName)
{
   if (form) {
        if (!form->GetBaseObject())
            return nullptr;

        auto                                      vm = RE::SkyrimVM::GetSingleton()->impl;
        auto                                      handlePolicy = vm.get()->GetObjectHandlePolicy();
        auto                                      handle = handlePolicy->GetHandleForObject(form->GetFormType(), form);

      if (handle != handlePolicy->EmptyHandle())
      {
            CAHZGetScriptVariableFunctor functor(variableName);
            vm->ForEachBoundObject(handle, &functor);
            auto     variable = functor.GetScriptVariable();

            if (variable && variable->IsObject()) {
                auto retForm = variable->Unpack<RE::TESForm*>();
                return retForm;
            }
      }
   }

   return nullptr;
}
