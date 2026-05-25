// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImXConsolePanel.h"

#include "GenericSingletons.h"
#include "ImSlateExtra.h"
#include "XConsoleManager.h"
#include "XConsoleCommandMeta.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if GMP_EXTEND_CONSOLE

static FXConsoleCommandLambdaFull XVar_ImSlateXConsole(
	TEXT("imslate.XConsole"),
	TEXT("Toggle ImSlate XConsole Panel"),
	[](TOptional<bool> bOpen, UWorld* InWorld, FOutputDevice& Ar) {
		if (InWorld)
			UGenericSingletons::GetSingleton<UImXConsolePanel>(InWorld)->EnableTick(bOpen);
	});

//////////////////////////////////////////////////////////////////////////
// Lifecycle
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::EnableTick(TOptional<bool> bEnable)
{
	do
	{
		if (!GetWorld())
			break;
		bool bTargetOpen = bEnable.Get(!bOpen);
		if (bOpen != bTargetOpen)
		{
			TickHandle.Reset();
			EndShow();
		}
		if (bTargetOpen)
		{
			TickHandle = ImSlate::ImSlateTicker::BindDelegate(
				ImSlate::ImSlateTicker::FOnTick::CreateWeakLambda(this, [this](float Delta) { Tick(Delta); }),
				GetWorld());
			StartShow();
		}
		return;
	} while (false);
}

void UImXConsolePanel::StartShow()
{
	bOpen = true;
	bNeedsRefresh = true;
}

void UImXConsolePanel::EndShow()
{
	bOpen = false;
	CommandTree.Reset();
	VariableTree.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Data - shared helpers
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::ParseDotName(const FString& FullName, FString& OutCategory, FString& OutSubCategory, FString& OutLeafName)
{
	TArray<FString> Parts;
	FullName.ParseIntoArray(Parts, TEXT("."));
	if (Parts.Num() >= 3)
	{
		OutCategory = Parts[0];
		OutSubCategory = Parts[1];
		OutLeafName = FullName.Mid(OutCategory.Len() + 1 + OutSubCategory.Len() + 1);
	}
	else if (Parts.Num() == 2)
	{
		OutCategory = Parts[0];
		OutLeafName = Parts[1];
	}
	else
	{
		OutCategory = TEXT("misc");
		OutLeafName = FullName;
	}
}

void UImXConsolePanel::RefreshAll()
{
	RefreshCommands();
	RefreshVariables();
	bNeedsRefresh = false;
}

//////////////////////////////////////////////////////////////////////////
// Data - Commands
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::RefreshCommands()
{
	CommandTree.Reset();

	auto& Manager = IXConsoleManager::Get();
	TArray<FString> CmdNames = Manager.GetXConsoleCommandList();
	CmdNames.Sort();

	for (const FString& CmdName : CmdNames)
	{
		FImXConsoleCommandInfo Info;
		Info.Name = CmdName;

		if (const GMP::FArrayTypeNames* Props = Manager.GetXConsoleCommandProps(*CmdName))
		{
			Info.ParamTypes.Append(Props->GetData(), Props->Num());
			Info.ParamValues.SetNum(Props->Num());
			Info.ParamEnabled.SetNum(Props->Num());
			for (int32 i = 0; i < Props->Num(); ++i)
				Info.ParamEnabled[i] = !(*Props)[i].ToString().StartsWith(TEXT("TOptional<"));
		}

		if (IConsoleObject* CObj = IConsoleManager::Get().FindConsoleObject(*CmdName))
			Info.Help = CObj->GetHelp();

		ParseDotName(CmdName, Info.Category, Info.SubCategory, Info.LeafName);

		// Apply meta: Category override, Hidden filter, DefaultValue
		if (const FXConsoleObjectMeta* Meta = IXConsoleManager::GetXConsoleMeta(*CmdName))
		{
			if (Meta->SelfMeta.GetMetaBool(TEXT("Hidden")))
				continue;
			if (Meta->SelfMeta.HasMeta(TEXT("Category")))
			{
				FString OverrideCategory = Meta->SelfMeta.GetMeta(TEXT("Category"));
				ParseDotName(OverrideCategory + TEXT(".") + Info.LeafName, Info.Category, Info.SubCategory, Info.LeafName);
			}
			// Apply DefaultValue for params
			for (int32 i = 0; i < Meta->Params.Num() && i < Info.ParamValues.Num(); ++i)
			{
				if (Meta->Params[i].HasMeta(TEXT("DefaultValue")) && Info.ParamValues[i].IsEmpty())
					Info.ParamValues[i] = Meta->Params[i].GetMeta(TEXT("DefaultValue"));
			}
		}

		CommandTree.FindOrAdd(Info.Category).FindOrAdd(Info.SubCategory).Add(MoveTemp(Info));
	}
}

//////////////////////////////////////////////////////////////////////////
// Data - Variables
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::RefreshVariables()
{
	VariableTree.Reset();

	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
		FConsoleObjectVisitor::CreateLambda([this](const TCHAR* Name, IConsoleObject* Obj) {
			IConsoleVariable* CVar = Obj->AsVariable();
			if (!CVar) return;

			FImXConsoleVariableInfo Info;
			Info.Name = Name;
			Info.Help = Obj->GetHelp();
			Info.CVar = CVar;
			Info.CurrentValue = CVar->GetString();

			ParseDotName(Info.Name, Info.Category, Info.SubCategory, Info.LeafName);
			VariableTree.FindOrAdd(Info.Category).FindOrAdd(Info.SubCategory).Add(MoveTemp(Info));
		}),
		TEXT(""));
}

//////////////////////////////////////////////////////////////////////////
// Execute
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::ExecuteCommand(const FImXConsoleCommandInfo& Info)
{
	FString FullCmd = Info.Name;
	for (int32 i = 0; i < Info.ParamValues.Num(); ++i)
	{
		const FString& TypeStr = Info.ParamTypes[i].ToString();
		bool bIsOptional = TypeStr.StartsWith(TEXT("TOptional<"));

		if (bIsOptional && !Info.ParamEnabled[i])
		{
			FullCmd += TEXT(" \"\"");
			continue;
		}

		const FString& Val = Info.ParamValues[i];
		if (Val.Contains(TEXT(" ")))
			FullCmd += FString::Printf(TEXT(" \"%s\""), *Val);
		else
			FullCmd += TEXT(" ") + Val;
	}

	if (UWorld* World = GetWorld())
	{
		GEngine->Exec(World, *FullCmd);
		UE_LOG(LogTemp, Log, TEXT("[ImXConsole] Executed: %s"), *FullCmd);
	}
}

//////////////////////////////////////////////////////////////////////////
// DrawParamWidget
//////////////////////////////////////////////////////////////////////////

namespace
{
	FString ExtractEnumName(const FString& TypeStr)
	{
		for (const TCHAR* Prefix : {TEXT("TEnumAsByte<"), TEXT("TEnum2Bytes<"), TEXT("TEnum4Bytes<"), TEXT("TEnum8Bytes<")})
		{
			if (TypeStr.StartsWith(Prefix) && TypeStr.EndsWith(TEXT(">")))
			{
				int32 PrefixLen = FCString::Strlen(Prefix);
				return TypeStr.Mid(PrefixLen, TypeStr.Len() - PrefixLen - 1);
			}
		}
		return FString();
	}

	FString ExtractArrayElementType(const FString& TypeStr)
	{
		if (TypeStr.StartsWith(TEXT("TArray<")) && TypeStr.EndsWith(TEXT(">")))
			return TypeStr.Mid(7, TypeStr.Len() - 8);
		return FString();
	}

	// Parse JSON array "[v1,v2,v3]" → TArray<FString>
	void ParseJsonArray(const FString& JsonStr, TArray<FString>& OutElements)
	{
		OutElements.Reset();
		FString Trimmed = JsonStr.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
			Trimmed = Trimmed.Mid(1, Trimmed.Len() - 2);
		if (!Trimmed.IsEmpty())
			Trimmed.ParseIntoArray(OutElements, TEXT(","));
		for (FString& Elem : OutElements)
			Elem = Elem.TrimStartAndEnd().TrimChar('"');
	}

	// TArray<FString> → JSON array string
	FString ToJsonArray(const TArray<FString>& Elements, bool bIsString)
	{
		FString Result = TEXT("[");
		for (int32 i = 0; i < Elements.Num(); ++i)
		{
			if (i > 0) Result += TEXT(",");
			if (bIsString)
				Result += FString::Printf(TEXT("\"%s\""), *Elements[i]);
			else
				Result += Elements[i];
		}
		Result += TEXT("]");
		return Result;
	}
}

void UImXConsolePanel::DrawParamWidget(const FName& TypeName, FString& Value, bool& bEnabled, bool bIsOptional, int32 Index, const FString& CmdName)
{
	FString WidgetId = FString::Printf(TEXT("%s_p%d"), *CmdName, Index);
	const FString TypeStr = TypeName.ToString();

	// Query meta for this parameter
	const FXConsoleObjectMeta* CmdMeta = IXConsoleManager::GetXConsoleMeta(*CmdName);
	const FXConsoleParamMeta* PMeta = (CmdMeta && CmdMeta->Params.IsValidIndex(Index)) ? &CmdMeta->Params[Index] : nullptr;

	// TOptional enable checkbox
	if (bIsOptional)
	{
		FString OptId = FString::Printf(TEXT("%s_opt%d"), *CmdName, Index);
		ImSlate::CheckBox(FStringView(OptId), bEnabled);
		ImSlate::SameLine();
	}

	// Extract inner type for TOptional<T>
	FString InnerType = TypeStr;
	if (TypeStr.StartsWith(TEXT("TOptional<")) && TypeStr.EndsWith(TEXT(">")))
		InnerType = TypeStr.Mid(10, TypeStr.Len() - 11);

	// === bool ===
	if (InnerType == TEXT("bool"))
	{
		bool bVal = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
		if (ImSlate::CheckBox(FStringView(WidgetId), bVal))
			Value = bVal ? TEXT("true") : TEXT("false");
	}
	// === float / double ===
	else if (InnerType == TEXT("float") || InnerType == TEXT("double"))
	{
		bool bHasUIRange = PMeta && PMeta->HasMeta(TEXT("UIMin")) && PMeta->HasMeta(TEXT("UIMax"));

		if (bHasUIRange)
		{
			// NumericFloat with slider
			TOptional<float> FloatOpt = Value.IsEmpty() ? TOptional<float>() : TOptional<float>(FCString::Atof(*Value));
			float ValMin = PMeta->GetMetaDouble(TEXT("ClampMin"), FLT_MIN);
			float ValMax = PMeta->GetMetaDouble(TEXT("ClampMax"), FLT_MAX);
			float SliderMin = PMeta->GetMetaDouble(TEXT("UIMin"), 0.f);
			float SliderMax = PMeta->GetMetaDouble(TEXT("UIMax"), 1.f);
			float Delta = PMeta->GetMetaDouble(TEXT("Delta"), 0.f);
			ImSlate::SetNextItemMinWidth(150.f);
			if (ImSlate::NumericFloat(FStringView(WidgetId), FloatOpt, ValMin, ValMax, SliderMin, SliderMax, Delta) != ImSlate::ImSliderStatus_Normal)
			{
				if (FloatOpt.IsSet())
					Value = FString::SanitizeFloat(FloatOpt.GetValue());
			}
		}
		else
		{
			// InputFloat with clamp
			double FloatVal = FCString::Atod(*Value);
			double MinVal = FLT_MIN, MaxVal = FLT_MAX;
			if (PMeta)
			{
				if (PMeta->HasMeta(TEXT("ClampMin"))) MinVal = PMeta->GetMetaDouble(TEXT("ClampMin"), FLT_MIN);
				if (PMeta->HasMeta(TEXT("ClampMax"))) MaxVal = PMeta->GetMetaDouble(TEXT("ClampMax"), FLT_MAX);
			}
			ImSlate::SetNextItemMinWidth(100.f);
			if (ImSlate::InputFloat(FStringView(WidgetId), FloatVal, MinVal, MaxVal) != ImSlate::ImSliderStatus_Normal)
			{
				if (PMeta)
				{
					if (PMeta->HasMeta(TEXT("ClampMin"))) FloatVal = FMath::Max(FloatVal, PMeta->GetMetaDouble(TEXT("ClampMin")));
					if (PMeta->HasMeta(TEXT("ClampMax"))) FloatVal = FMath::Min(FloatVal, PMeta->GetMetaDouble(TEXT("ClampMax")));
				}
				Value = FString::SanitizeFloat(FloatVal);
			}
		}
	}
	// === int types ===
	else if (InnerType == TEXT("int32") || InnerType == TEXT("int16") || InnerType == TEXT("int8")
		|| InnerType == TEXT("uint8") || InnerType == TEXT("uint16") || InnerType == TEXT("uint32"))
	{
		bool bHasUIRange = PMeta && PMeta->HasMeta(TEXT("UIMin")) && PMeta->HasMeta(TEXT("UIMax"));

		if (bHasUIRange)
		{
			// NumericInt with slider
			TOptional<int32> IntOpt = Value.IsEmpty() ? TOptional<int32>() : TOptional<int32>(FCString::Atoi(*Value));
			int32 ValMin = PMeta->GetMetaInt(TEXT("ClampMin"), INT_MIN);
			int32 ValMax = PMeta->GetMetaInt(TEXT("ClampMax"), INT_MAX);
			int32 SliderMin = PMeta->GetMetaInt(TEXT("UIMin"), 0);
			int32 SliderMax = PMeta->GetMetaInt(TEXT("UIMax"), 100);
			int32 Delta = PMeta->GetMetaInt(TEXT("Delta"), 0);
			ImSlate::SetNextItemMinWidth(150.f);
			if (ImSlate::NumericInt(FStringView(WidgetId), IntOpt, ValMin, ValMax, SliderMin, SliderMax, Delta) != ImSlate::ImSliderStatus_Normal)
			{
				if (IntOpt.IsSet())
					Value = FString::FromInt(IntOpt.GetValue());
			}
		}
		else
		{
			// InputText with clamp
			FString IntStr = Value;
			ImSlate::SetNextItemMinWidth(80.f);
			if (ImSlate::InputText(FStringView(WidgetId), IntStr))
			{
				int32 IntVal = FCString::Atoi(*IntStr);
				if (PMeta)
				{
					if (PMeta->HasMeta(TEXT("ClampMin"))) IntVal = FMath::Max(IntVal, PMeta->GetMetaInt(TEXT("ClampMin")));
					if (PMeta->HasMeta(TEXT("ClampMax"))) IntVal = FMath::Min(IntVal, PMeta->GetMetaInt(TEXT("ClampMax")));
				}
				Value = FString::FromInt(IntVal);
			}
		}
	}
	// === enum (single select or bitmask multi-select) ===
	else if (FString EnumName = ExtractEnumName(InnerType); !EnumName.IsEmpty())
	{
		UEnum* EnumPtr = DynamicEnum(EnumName);
		if (!EnumPtr && PMeta && PMeta->HasMeta(TEXT("BitmaskEnum")))
			EnumPtr = DynamicEnum(PMeta->GetMeta(TEXT("BitmaskEnum")));

		bool bIsBitmask = PMeta && PMeta->GetMetaBool(TEXT("Bitmask"));

		if (EnumPtr && bIsBitmask)
		{
			// Bitmask multi-select: checkboxes per enum value
			int32 MaskVal = Value.IsEmpty() ? 0 : FCString::Atoi(*Value);
			int32 NumEntries = EnumPtr->NumEnums() - 1;
			bool bChanged = false;
			for (int32 i = 0; i < NumEntries; ++i)
			{
				int64 EntryVal = EnumPtr->GetValueByIndex(i);
				if (EntryVal == 0 && i > 0) continue;

				FString EntryName = EnumPtr->GetDisplayNameTextByIndex(i).ToString();
				if (EntryName.IsEmpty()) EntryName = EnumPtr->GetNameStringByIndex(i);

				FString CheckId = FString::Printf(TEXT("%s_bm%d"), *WidgetId, i);
				bool bBit = !!(MaskVal & (int32)EntryVal);
				if (ImSlate::CheckBox(FStringView(CheckId), bBit))
				{
					if (bBit) MaskVal |= (int32)EntryVal;
					else MaskVal &= ~(int32)EntryVal;
					bChanged = true;
				}
				ImSlate::SameLine();
				ImSlate::Text(FStringView(CheckId), EntryName);
			}
			if (bChanged) Value = FString::FromInt(MaskVal);
		}
		else if (EnumPtr)
		{
			// Single select combo
			int64 CurVal = Value.IsEmpty() ? 0 : (int64)FCString::Atoi(*Value);
			ImSlate::SetNextItemMinWidth(120.f);
			if (ImSlate::ComboBoxForEnum(FStringView(WidgetId), CurVal, EnumPtr))
				Value = FString::FromInt((int32)CurVal);
		}
		else
		{
			ImSlate::SetNextItemMinWidth(100.f);
			ImSlate::InputText(FStringView(WidgetId), Value);
		}
	}
	// === Object / SoftObject ===
	else if (InnerType.Contains(TEXT("ObjectProperty")) || InnerType.Contains(TEXT("SoftObjectProperty")))
	{
		FSoftObjectPath Path(Value);
		ImSlate::SetNextItemMinWidth(200.f);
		if (ImSlate::AssetPicker(FStringView(WidgetId), Path))
			Value = Path.ToString();
	}
	// === Class / SoftClass ===
	else if (InnerType.Contains(TEXT("ClassProperty")) || InnerType.Contains(TEXT("SoftClassProperty")))
	{
		FSoftClassPath Path(Value);
		ImSlate::SetNextItemMinWidth(200.f);
		if (ImSlate::ClassPicker(FStringView(WidgetId), Path))
			Value = Path.ToString();
	}
	// === TArray<T> ===
	else if (FString ElemType = ExtractArrayElementType(InnerType); !ElemType.IsEmpty())
	{
		TArray<FString> Elements;
		ParseJsonArray(Value, Elements);

		bool bIsStringType = (ElemType == TEXT("String") || ElemType == TEXT("FString") || ElemType == TEXT("FName") || ElemType == TEXT("FText"));
		bool bChanged = false;

		// Draw each element
		for (int32 i = 0; i < Elements.Num(); ++i)
		{
			FString ElemId = FString::Printf(TEXT("%s_e%d"), *WidgetId, i);
			bool bDummy = true;
			ImSlate::SetNextItemMinWidth(80.f);
			DrawParamWidget(FName(*ElemType), Elements[i], bDummy, false, i, WidgetId);

			// Remove button
			ImSlate::SameLine();
			FString RemoveId = FString::Printf(TEXT("X##%s_r%d"), *WidgetId, i);
			if (ImSlate::Button(FStringView(RemoveId)))
			{
				Elements.RemoveAt(i);
				bChanged = true;
				--i;
			}
		}

		// Add button
		FString AddId = FString::Printf(TEXT("+##%s_add"), *WidgetId);
		if (ImSlate::Button(FStringView(AddId)))
		{
			Elements.Add(FString());
			bChanged = true;
		}

		if (bChanged)
			Value = ToJsonArray(Elements, bIsStringType);
	}
	// === string / name / other ===
	else
	{
		ImSlate::SetNextItemFillWidth(1.f);
		ImSlate::InputText(FStringView(WidgetId), Value);
	}
}

//////////////////////////////////////////////////////////////////////////
// UI - Search bar
//////////////////////////////////////////////////////////////////////////

bool UImXConsolePanel::MatchesSearch(const FString& Name, const FString& Help) const
{
	if (SearchFilter.IsEmpty()) return true;
	return Name.Contains(SearchFilter, ESearchCase::IgnoreCase) ||
		   Help.Contains(SearchFilter, ESearchCase::IgnoreCase);
}

static constexpr float XConsoleRowHeight = 48.f;

bool UImXConsolePanel::DrawFold(const FString& Id, const FString& DisplayText)
{
	bool& bFolded = FoldStates.FindOrAdd(Id, true);
	FString Arrow = bFolded ? TEXT("\x25B6 ") : TEXT("\x25BC ");
	ImSlate::SetNextItemFillWidth(1.f);
	if (ImSlate::TextButton(FStringView(Id), FText::FromString(Arrow + DisplayText), ImVec2(0, XConsoleRowHeight)))
		bFolded = !bFolded;
	return !bFolded;
}

void UImXConsolePanel::DrawSearchBar()
{
	static TArray<FString> SearchNames;
	if (bNeedsRefresh || SearchNames.IsEmpty())
	{
		SearchNames.Reset();
		for (auto& [Cat, SubCats] : CommandTree)
			for (auto& [Sub, Cmds] : SubCats)
				for (auto& Cmd : Cmds)
					SearchNames.Add(Cmd.LeafName);
		for (auto& [Cat, SubCats] : VariableTree)
			for (auto& [Sub, Vars] : SubCats)
				for (auto& Var : Vars)
					SearchNames.Add(Var.LeafName);
	}

	ImSlate::SetNextItemFillWidth(0.7f);
	ImSlate::SearchBox("##XConsoleSearch", SearchFilter, &SearchNames, nullptr, ImVec2(0, XConsoleRowHeight));

	ImSlate::SameLine();
	if (ImSlate::Button("Refresh", ImVec2(80.f, XConsoleRowHeight)))
		bNeedsRefresh = true;
}

//////////////////////////////////////////////////////////////////////////
// UI - Command drawing
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::DrawCommandEntry(FImXConsoleCommandInfo& Info)
{
	IM_SLATE_SCOPE(FStringView(Info.Name));

	const FXConsoleObjectMeta* Meta = IXConsoleManager::GetXConsoleMeta(*Info.Name);

	// Command name — click to execute
	FString LeafDisplay = (Meta && Meta->SelfMeta.HasMeta(TEXT("DisplayName")))
		? Meta->SelfMeta.GetMeta(TEXT("DisplayName"))
		: Info.LeafName;
	if (ImSlate::TextButton("CmdName", FText::FromString(LeafDisplay), ImVec2(0, XConsoleRowHeight)))
		ExecuteCommand(Info);

	// Parameters — each on same line, fill remaining width
	for (int32 i = 0; i < Info.ParamTypes.Num(); ++i)
	{
		ImSlate::SameLine();
		const FString& TypeStr = Info.ParamTypes[i].ToString();
		bool bIsOptional = TypeStr.StartsWith(TEXT("TOptional<"));
		DrawParamWidget(Info.ParamTypes[i], Info.ParamValues[i], Info.ParamEnabled[i], bIsOptional, i, Info.Name);
	}

	ImSlate::Spacing(3.f);
}

void UImXConsolePanel::DrawCommandCategory(const FString& Category, TMap<FString, TArray<FImXConsoleCommandInfo>>& SubCats)
{
	if (!SearchFilter.IsEmpty())
	{
		bool bAnyVisible = false;
		for (auto& SubPair : SubCats)
			for (const auto& Cmd : SubPair.Value)
				if (MatchesSearch(Cmd.Name, Cmd.Help)) { bAnyVisible = true; break; }
		if (!bAnyVisible) return;
	}

	if (ImSlate::BeginFold(FStringView(Category), FText::FromString(Category)))
	{
		TArray<FString> SubCatKeys;
		SubCats.GetKeys(SubCatKeys);
		SubCatKeys.Sort();

		for (const FString& SubCat : SubCatKeys)
		{
			TArray<FImXConsoleCommandInfo>& Commands = SubCats[SubCat];

			if (SubCat.IsEmpty())
			{
				for (auto& Cmd : Commands)
					if (MatchesSearch(Cmd.Name, Cmd.Help))
						DrawCommandEntry(Cmd);
			}
			else
			{
				bool bSubVisible = SearchFilter.IsEmpty();
				if (!bSubVisible)
					for (const auto& Cmd : Commands)
						if (MatchesSearch(Cmd.Name, Cmd.Help)) { bSubVisible = true; break; }
				if (!bSubVisible) continue;

				FString SubId = Category + TEXT(".") + SubCat;
				if (ImSlate::BeginFold(FStringView(SubId), FText::FromString(SubCat)))
				{
					for (auto& Cmd : Commands)
						if (MatchesSearch(Cmd.Name, Cmd.Help))
							DrawCommandEntry(Cmd);
					ImSlate::EndFold();
				}
			}
		}
		ImSlate::EndFold();
	}
}

void UImXConsolePanel::DrawCommandsTab()
{
	TArray<FString> Categories;
	CommandTree.GetKeys(Categories);
	Categories.Sort();

	for (int32 i = 0; i < Categories.Num(); ++i)
	{
		DrawCommandCategory(Categories[i], CommandTree[Categories[i]]);
		if (i < Categories.Num() - 1)
			ImSlate::Spacing(2.f);
	}
}

//////////////////////////////////////////////////////////////////////////
// UI - Variable drawing
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::DrawVariableEntry(FImXConsoleVariableInfo& Info)
{
	if (!Info.CVar) return;

	IM_SLATE_SCOPE(FStringView(Info.Name));

	const FXConsoleObjectMeta* Meta = IXConsoleManager::GetXConsoleMeta(*Info.Name);
	const FXConsoleParamMeta* SelfMeta = Meta ? &Meta->SelfMeta : nullptr;

	FString DisplayLabel = (SelfMeta && SelfMeta->HasMeta(TEXT("DisplayName")))
		? SelfMeta->GetMeta(TEXT("DisplayName"))
		: Info.LeafName;
	ImSlate::TextButton("VarName", FText::FromString(DisplayLabel), ImVec2(0, XConsoleRowHeight));
	ImSlate::SameLine();

	FString WidgetId = FString::Printf(TEXT("##val_%s"), *Info.Name);

	if (Info.CVar->IsVariableBool())
	{
		bool bVal = Info.CVar->GetBool();
		if (ImSlate::CheckBox(FStringView(WidgetId), bVal))
			Info.CVar->Set(bVal ? 1 : 0);
	}
	else if (Info.CVar->IsVariableFloat())
	{
		bool bHasUIRange = SelfMeta && SelfMeta->HasMeta(TEXT("UIMin")) && SelfMeta->HasMeta(TEXT("UIMax"));
		if (bHasUIRange)
		{
			TOptional<float> FloatOpt = Info.CVar->GetFloat();
			float ValMin = SelfMeta->GetMetaDouble(TEXT("ClampMin"), FLT_MIN);
			float ValMax = SelfMeta->GetMetaDouble(TEXT("ClampMax"), FLT_MAX);
			float SliderMin = SelfMeta->GetMetaDouble(TEXT("UIMin"), 0.f);
			float SliderMax = SelfMeta->GetMetaDouble(TEXT("UIMax"), 1.f);
			ImSlate::SetNextItemMinWidth(150.f);
			if (ImSlate::NumericFloat(FStringView(WidgetId), FloatOpt, ValMin, ValMax, SliderMin, SliderMax) != ImSlate::ImSliderStatus_Normal)
			{
				if (FloatOpt.IsSet())
					Info.CVar->Set(FloatOpt.GetValue());
			}
		}
		else
		{
			double Val = (double)Info.CVar->GetFloat();
			double MinVal = FLT_MIN, MaxVal = FLT_MAX;
			if (SelfMeta)
			{
				if (SelfMeta->HasMeta(TEXT("ClampMin"))) MinVal = SelfMeta->GetMetaDouble(TEXT("ClampMin"));
				if (SelfMeta->HasMeta(TEXT("ClampMax"))) MaxVal = SelfMeta->GetMetaDouble(TEXT("ClampMax"));
			}
			ImSlate::SetNextItemMinWidth(100.f);
			if (ImSlate::InputFloat(FStringView(WidgetId), Val, MinVal, MaxVal) != ImSlate::ImSliderStatus_Normal)
			{
				if (SelfMeta)
				{
					if (SelfMeta->HasMeta(TEXT("ClampMin"))) Val = FMath::Max(Val, SelfMeta->GetMetaDouble(TEXT("ClampMin")));
					if (SelfMeta->HasMeta(TEXT("ClampMax"))) Val = FMath::Min(Val, SelfMeta->GetMetaDouble(TEXT("ClampMax")));
				}
				Info.CVar->Set((float)Val);
			}
		}
	}
	else if (Info.CVar->IsVariableInt())
	{
		FString ValStr = Info.CVar->GetString();
		ImSlate::SetNextItemMinWidth(100.f);
		if (ImSlate::InputText(FStringView(WidgetId), ValStr))
		{
			int32 IntVal = FCString::Atoi(*ValStr);
			if (SelfMeta)
			{
				if (SelfMeta->HasMeta(TEXT("ClampMin"))) IntVal = FMath::Max(IntVal, SelfMeta->GetMetaInt(TEXT("ClampMin")));
				if (SelfMeta->HasMeta(TEXT("ClampMax"))) IntVal = FMath::Min(IntVal, SelfMeta->GetMetaInt(TEXT("ClampMax")));
			}
			Info.CVar->Set(IntVal);
		}
	}
	else
	{
		FString ValStr = Info.CVar->GetString();
		ImSlate::SetNextItemMinWidth(150.f);
		if (ImSlate::InputText(FStringView(WidgetId), ValStr))
			Info.CVar->Set(*ValStr);
	}

	ImSlate::Spacing(3.f);
}

void UImXConsolePanel::DrawVariableCategory(const FString& Category, TMap<FString, TArray<FImXConsoleVariableInfo>>& SubCats)
{
	if (!SearchFilter.IsEmpty())
	{
		bool bAnyVisible = false;
		for (auto& SubPair : SubCats)
			for (const auto& Var : SubPair.Value)
				if (MatchesSearch(Var.Name, Var.Help)) { bAnyVisible = true; break; }
		if (!bAnyVisible) return;
	}

	if (ImSlate::BeginFold(FStringView(Category), FText::FromString(Category)))
	{
		TArray<FString> SubCatKeys;
		SubCats.GetKeys(SubCatKeys);
		SubCatKeys.Sort();

		for (const FString& SubCat : SubCatKeys)
		{
			TArray<FImXConsoleVariableInfo>& Vars = SubCats[SubCat];

			if (SubCat.IsEmpty())
			{
				for (auto& Var : Vars)
					if (MatchesSearch(Var.Name, Var.Help))
						DrawVariableEntry(Var);
			}
			else
			{
				bool bSubVisible = SearchFilter.IsEmpty();
				if (!bSubVisible)
					for (const auto& Var : Vars)
						if (MatchesSearch(Var.Name, Var.Help)) { bSubVisible = true; break; }
				if (!bSubVisible) continue;

				FString SubId = Category + TEXT(".") + SubCat;
				if (ImSlate::BeginFold(FStringView(SubId), FText::FromString(SubCat)))
				{
					for (auto& Var : Vars)
						if (MatchesSearch(Var.Name, Var.Help))
							DrawVariableEntry(Var);
					ImSlate::EndFold();
				}
			}
		}
		ImSlate::EndFold();
	}
}

void UImXConsolePanel::DrawVariablesTab()
{
	TArray<FString> Categories;
	VariableTree.GetKeys(Categories);
	Categories.Sort();

	for (int32 i = 0; i < Categories.Num(); ++i)
	{
		DrawVariableCategory(Categories[i], VariableTree[Categories[i]]);
		if (i < Categories.Num() - 1)
			ImSlate::Spacing(2.f);
	}
}

//////////////////////////////////////////////////////////////////////////
// Tick
//////////////////////////////////////////////////////////////////////////

void UImXConsolePanel::Tick(float Delta)
{
	if (!bOpen) return;

	if (bNeedsRefresh)
		RefreshAll();

	const FVector2D WindowSize = FVector2D(700.f, 800.f);

	ImSlate::SetNextWindowPos(FVector2D(400, 300), ImSlateCond_Once, FVector2D(0.5f, 0.5f));
	ImSlate::SetNextWindowSize(WindowSize, ImSlateCond_Once);
	ImSlate::SetNextWindowBgAlpha(0.92f);

	ImSlate::Begin("XConsole Commands", &bOpen);

	// Search bar
	DrawSearchBar();

	ImSlate::Spacing(6.f);

	// Tab buttons — use TextButton for better visibility
	{
		FString CmdLabel = FString::Printf(TEXT("Commands (%d)"), [this]() {
			int32 N = 0;
			for (auto& C : CommandTree) for (auto& S : C.Value) N += S.Value.Num();
			return N;
		}());
		FString VarLabel = FString::Printf(TEXT("Variables (%d)"), [this]() {
			int32 N = 0;
			for (auto& C : VariableTree) for (auto& S : C.Value) N += S.Value.Num();
			return N;
		}());

		ImSlate::SetNextItemMinWidth(150.f);
		if (ImSlate::TextButton(ActiveTab == 0 ? "##TabCmd_Active" : "##TabCmd",
				FText::FromString(ActiveTab == 0 ? FString::Printf(TEXT("[%s]"), *CmdLabel) : CmdLabel)))
			ActiveTab = 0;

		ImSlate::SameLine();
		ImSlate::SetNextItemMinWidth(150.f);
		if (ImSlate::TextButton(ActiveTab == 1 ? "##TabVar_Active" : "##TabVar",
				FText::FromString(ActiveTab == 1 ? FString::Printf(TEXT("[%s]"), *VarLabel) : VarLabel)))
			ActiveTab = 1;
	}

	ImSlate::Spacing(6.f);

	// Content
	if (ActiveTab == 0)
		DrawCommandsTab();
	else
		DrawVariablesTab();

	ImSlate::End();

	if (!bOpen)
		EnableTick(false);
}

#endif // GMP_EXTEND_CONSOLE
