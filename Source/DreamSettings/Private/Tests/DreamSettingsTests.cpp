#if WITH_DEV_AUTOMATION_TESTS
#include "DreamSettingsModel.h"
#include "DreamSettingsIniStore.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

namespace
{
	struct FMemoryProvider : IDreamSettingProvider
	{
		double Value = 0.4;
		bool bFail = false;
		bool bPartialFailure = false;
		FDreamSettingsResult Read(FDreamSettingValue& Out) const override { Out = Value; return {}; }
		FDreamSettingsResult Write(const FDreamSettingValue& Input) override
		{
			const double In = Input.Number;
			if (bPartialFailure)
			{
				if (!FMath::IsNearlyEqual(In, 0.4)) Value = In;
				return FDreamSettingsResult::Fail(EDreamSettingsError::Backend, FText::FromString(TEXT("partial write; compensation unavailable")));
			}
			if (bFail) return FDreamSettingsResult::Fail(EDreamSettingsError::Backend, FText::FromString(TEXT("injected backend failure")));
			Value = In;
			return {};
		}
	};
	struct FMemoryStore : IDreamSettingsStore
	{
		FDreamSettingsValues Values;
		bool bFail = false;
		FDreamSettingsResult Load(FDreamSettingsValues& Out) override { Out = Values; return {}; }
		FDreamSettingsResult Save(const FDreamSettingsValues& In) override
		{
			if (bFail) return FDreamSettingsResult::Fail(EDreamSettingsError::Storage, FText::FromString(TEXT("injected disk failure")));
			Values = In;
			return {};
		}
	};
	struct FFixture
	{
		TSharedRef<FDreamSettingsRegistry> Registry = MakeShared<FDreamSettingsRegistry>();
		TSharedRef<FMemoryProvider> Audio = MakeShared<FMemoryProvider>();
		TSharedRef<FMemoryProvider> Display = MakeShared<FMemoryProvider>();
		TSharedRef<FMemoryStore> Store = MakeShared<FMemoryStore>();
		FFixture()
		{
			FDreamSettingDefinition D;
			D.Id = DreamSettingIds::MasterVolume;
			D.Default = 1;
			D.bPreview = true;
			Registry->Register(D, Audio);
			D.Id = DreamSettingIds::VSync;
			D.Kind = EDreamSettingKind::Toggle;
			D.Step = 1;
			D.Default = 0;
			D.bPreview = false;
			Display->Value = 0;
			Registry->Register(D, Display);
		}
		TSharedRef<FDreamSettingsSession> Open()
		{
			auto S = MakeShared<FDreamSettingsSession>(Registry, Store);
			S->Open();
			return S;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsSessionTest, "DreamSettings.Core.Session", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsSessionTest::RunTest(const FString&)
{
	FFixture F;
	{
		auto S = F.Open();
		TestFalse(TEXT("non-default current value is clean"), S->IsDirty());
		TestTrue(TEXT("preview accepted"), S->Stage(DreamSettingIds::MasterVolume, 0.72).IsOk());
		TestEqual(TEXT("preview reaches backend"), F.Audio->Value, 0.72);
		TestTrue(TEXT("dirty after preview"), S->IsDirty());
		TestTrue(TEXT("revert accepted"), S->Revert().IsOk());
		TestEqual(TEXT("revert restores opening value, not default"), F.Audio->Value, 0.4);
		TestFalse(TEXT("reverted clean"), S->IsDirty());
		TestFalse(TEXT("reject out of range"), S->Stage(DreamSettingIds::MasterVolume, 1.1).IsOk());
		TestFalse(TEXT("reject fractional toggle"), S->Stage(DreamSettingIds::VSync, 0.5).IsOk());
		S->Stage(DreamSettingIds::VSync, 1);
		TestEqual(TEXT("display is deferred"), F.Display->Value, 0.0);
		S->Stage(DreamSettingIds::MasterVolume, 0.66);
		TestTrue(TEXT("apply succeeds"), S->Apply().IsOk());
		TestEqual(TEXT("native value applied"), F.Display->Value, 1.0);
		TestEqual(TEXT("volume persisted"), F.Store->Values.FindChecked(DreamSettingIds::MasterVolume).Number, 0.66);
		TestFalse(TEXT("committed clean"), S->IsDirty());
		S->Stage(DreamSettingIds::MasterVolume, 0.21);
	}
	TestEqual(TEXT("destruction discards preview"), F.Audio->Value, 0.66);
	auto Reopened = F.Open();
	TestEqual(TEXT("reopened keeps committed value"), Reopened->GetPending().FindChecked(DreamSettingIds::MasterVolume).Number, 0.66);
	Reopened->Reset();
	TestTrue(TEXT("reset stages defaults"), Reopened->IsDirty());
	TestEqual(TEXT("reset does not silently apply"), F.Audio->Value, 0.66);
	Reopened->Revert();
	TestFalse(TEXT("reset is undoable"), Reopened->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsFailureTest, "DreamSettings.Core.Failures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsFailureTest::RunTest(const FString&)
{
	FFixture F;
	auto S = F.Open();
	S->Stage(DreamSettingIds::MasterVolume, 0.8);
	S->Stage(DreamSettingIds::VSync, 1);
	F.Store->bFail = true;
	TestTrue(TEXT("disk error exposed"), S->Apply().Code == EDreamSettingsError::Storage);
	TestEqual(TEXT("audio compensated"), F.Audio->Value, 0.4);
	TestEqual(TEXT("display compensated"), F.Display->Value, 0.0);
	TestTrue(TEXT("failed draft retained"), S->IsDirty());
	TestTrue(TEXT("nothing committed on disk failure"), F.Store->Values.IsEmpty());
	F.Store->bFail = false;
	F.Display->bFail = true;
	AddExpectedMessagePlain(TEXT("Apply Display.VSync failed: injected backend failure (actual 0 expected 1)"),
		ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1);
	TestTrue(TEXT("backend error exposed"), S->Apply().Code == EDreamSettingsError::Backend);
	TestEqual(TEXT("earlier provider compensated"), F.Audio->Value, 0.4);
	F.Display->bFail = false;
	TestTrue(TEXT("retry works"), S->Apply().IsOk());
	F.Audio->Value = 0.33;
	TestTrue(TEXT("external mutation blocks overwrite"), S->Apply().Code == EDreamSettingsError::Conflict);
	FFixture Partial;
	auto PartialSession = Partial.Open();
	Partial.Audio->bPartialFailure = true;
	TestTrue(TEXT("failed preview compensation surfaced"), PartialSession->Stage(DreamSettingIds::MasterVolume, 0.72).Code == EDreamSettingsError::Rollback);
	TestTrue(TEXT("unrestored backend is dirty even without a new draft"), PartialSession->IsDirty());
	TestFalse(TEXT("uncertain backend blocks commit"), PartialSession->Apply().IsOk());
	Partial.Audio->bPartialFailure = false;
	TestTrue(TEXT("explicit revert retries recovery"), PartialSession->Revert().IsOk());
	TestEqual(TEXT("partial write eventually restored"), Partial.Audio->Value, 0.4);
	TestFalse(TEXT("recovered session clean"), PartialSession->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsRegistryTest, "DreamSettings.Core.Registry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsRegistryTest::RunTest(const FString&)
{
	FDreamSettingsRegistry Registry;
	auto P = MakeShared<FMemoryProvider>();
	FDreamSettingDefinition D;
	D.Id = TEXT("A"); D.Dependencies = { TEXT("B") };
	TestTrue(TEXT("register forward dependency"), Registry.Register(D, P).IsOk());
	TestFalse(TEXT("duplicate rejected"), Registry.Register(D, P).IsOk());
	TArray<FName> Order;
	TestFalse(TEXT("missing dependency rejected"), Registry.Validate(Order).IsOk());
	D.Id = TEXT("B"); D.Dependencies = { TEXT("A") };
	Registry.Register(D, P);
	TestFalse(TEXT("cycle rejected"), Registry.Validate(Order).IsOk());
	D.Id = TEXT("C"); D.Step = 0;
	TestFalse(TEXT("zero step rejected"), Registry.Register(D, P).IsOk());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSettingsStoreTest, "DreamSettings.Core.Persistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSettingsStoreTest::RunTest(const FString&)
{
	const FString Filename = FPaths::ProjectSavedDir() / TEXT("Automation/DreamSettings") / (FGuid::NewGuid().ToString() + TEXT(".ini"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	FFileHelper::SaveStringToFile(TEXT(";METADATA=(Diff=true, UseCommands=true)\n[SectionsToSave]\nbCanSaveAllSections=false\nSection=/Script/Engine.GameUserSettings\n[OtherSystem]\nKeep=unchanged\n"), *Filename);
	FDreamSettingsIniStore Store(Filename, TEXT("/Script/Engine.GameUserSettings"));
	FDreamSettingsValues Written {{DreamSettingIds::MasterVolume, 0.37}, {DreamSettingIds::VSync, 1}};
	TestTrue(TEXT("atomic save"), Store.Save(Written).IsOk());
	FDreamSettingsIniStore ColdStore(Filename, TEXT("/Script/Engine.GameUserSettings"));
	FDreamSettingsValues Read;
	TestTrue(TEXT("cold load"), ColdStore.Load(Read).IsOk());
	TestEqual(TEXT("cold volume"), Read.FindRef(DreamSettingIds::MasterVolume).Number, 0.37);
	TestEqual(TEXT("native VSync saved"), Read.FindRef(DreamSettingIds::VSync).Number, 1.0);
	FString Text;
	FFileHelper::LoadFileToString(Text, *Filename);
	TestTrue(TEXT("unrelated config preserved"), Text.Contains(TEXT("Keep=unchanged")));
	const FString BeforeFailure = Text;
	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, true);
	Written[DreamSettingIds::MasterVolume] = 0.88;
	TestFalse(TEXT("read-only destination reports failure"), ColdStore.Save(Written).IsOk());
	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Filename, false);
	FFileHelper::LoadFileToString(Text, *Filename);
	TestEqual(TEXT("replacement failure preserves original bytes"), Text, BeforeFailure);
	const FString Future(TEXT("[DreamSettings.Preferences]\nSchemaVersion=999\nAudio.MasterVolume=0.5\n"));
	FFileHelper::SaveStringToFile(Future, *Filename);
	TestFalse(TEXT("future schema load rejected"), ColdStore.Load(Read).IsOk());
	TestFalse(TEXT("future schema not overwritten"), ColdStore.Save(Written).IsOk());
	FFileHelper::LoadFileToString(Text, *Filename);
	TestEqual(TEXT("file untouched"), Text, Future);
	IFileManager::Get().Delete(*Filename);
	return true;
}
#endif
