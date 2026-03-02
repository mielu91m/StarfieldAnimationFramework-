# Analiza zgodności z CommonLibSF (Starfield)

Przegląd plików pluginu SAF pod kątem spójności z API CommonLibSF. Wprowadzone poprawki i zalecenia.

---

## Wprowadzone poprawki

### 1. **Tasks/Input.cpp**
- **`GetOldFunction()`** – w `VFuncHook` (Util/General.h) metoda nazywa się **`GetOriginal()`**. Zmieniono wywołanie na `GetOriginal()`.
- **`curEvent->eventType.get()`** – w Starfield `RE::InputEvent::eventType` to zwykły enum (`EventType`), nie wrapper z `.get()`. Zmieniono na `curEvent->eventType`.

### 2. **Papyrus/EventManager**
- API było już dostosowane (Papyrus::VMHandle, RegisterScript/UnregisterScript, brak IFunctionArguments). EventManager.cpp usunięto z duplikatami starych metod.

### 3. **Papyrus/SAFScript.cpp**
- **EventType** jest w namespace `Papyrus`, nie w klasie EventManager. Poprawiono: `Papyrus::EventManager::EventType` → `Papyrus::EventType`.

### 4. **Commands/SAFCommand.cpp, Serialization, Util/Ozz**
- Wcześniejsze poprawki: LoadGLTF, CreateRawAnimation, CreateOptimizedAsset, GetNPC()->GetRace(), SkeletonMap, ToSV (data/size), RegisterForKey (ButtonCallback), brak ExportSAF/GetRace na PlayerCharacter.

---

## Pliki zgodne z CommonLibSF

- **PCH.h** – `#include <RE/Starfield.h>` daje dostęp do RE::.
- **Settings/Settings.h** – `#include "RE/Starfield.h"`, użycie RE::Actor.
- **Animation/GraphManager.h, Graph.h, Sequencer.h** – RE::Actor, RE::TESObjectREFR, RE::BSFixedString.
- **API (API_Internal.h, API_External.h, MCF_API.h)** – RE::Actor, RE::NiPoint3, RE::NiQuaternion, RE::NiAVObject, RE::NiPointer, RE::TESObjectREFR, RE::TESForm.
- **Util/String.h** – RE::BSFixedString (konstruktor z string_view jest w BSFixedString).
- **RE/Events.h** – lokalne zdarzenia, bez zależności od RE.

---

## Uwagi i potencjalne problemy

### 1. **Settings/SkeletonImpl.cpp**
- **GetSkeletonModel()** – celowo zwraca `nullptr` i loguje ostrzeżenie. W Starfield nie ma `RE::ModelDB` ani `TESRace::skeletonModel` jak w Skyrim/F4. Kod jest zabezpieczony (komentarz + early return). Do uzupełnienia: odpowiednik API Starfield do ładowania modelu szkieletu rasy (np. BSModelDB / BSResource2, jeśli kiedyś udostępnione).
- **FillInSkeletonNIFData** – po powrocie `GetSkeletonModel() == nullptr` korzysta z JSON; **RE::NiNode**, **RE::NiQuaternion** i `gameNode->local` są z CommonLibSF (RE/N/).

### 2. **Util/VM.h i VM.cpp**
- **RE::BSScript::GetVMTypeID\<T\>()** – w CommonLibSF jest w `RE::BSScript` (BSScriptUtil.h) i wymaga typu spełniającego `detail::object` (np. z FORMTYPE). Dla dowolnego `T*` (np. nie-TESForm) może się nie kompilować; używać tylko z typami z RE (Object, formy).
- **DispatchMethodCall** – sygnatura w Starfield:  
  `(std::uint64_t, const BSFixedString&, const BSFixedString&, const BSTThreadScrapFunction<bool(BSScrapArray<Variable>&)>&, const BSTSmartPointer<IStackCallbackFunctor>&, int)`.  
  W VM.h przekazywane są `string_view`; BSFixedString ma konstruktor z typu convertible do `std::string_view`, więc tworzenie tymczasowego BSFixedString przy wywołaniu jest OK.
- **BSTThreadScrapFunction** w IVirtualMachine.h to alias na `std::function<F>`, więc `std::function<bool(RE::BSScrapArray<...>&)>` jest zgodne.

### 3. **Util/VM.cpp – IStackCallbackFunctor**
- **FunctionCallback** dziedziczy po `RE::BSScript::IStackCallbackFunctor`. W Starfield interfejs może mieć inne metody wirtualne (CallQueued, CallCanceled, StartMultiDispatch, EndMultiDispatch, CanSave). Należy sprawdzić w RE/S/Stack.h / RE/V/VirtualMachine.h pełną listę i uzupełnić implementację, jeśli brakuje override’ów.

### 4. **Tasks/SaveLoadListener.cpp**
- **VFuncHook** i adresy (np. 481117) – typowe dla konkretnej wersji gry. Przy aktualizacji CommonLibSF / wersji Starfield trzeba zweryfikować relokacje/ID.

### 5. **Tasks/Input.cpp**
- **PerformInputProcessingHook(459729, 0x1, ...)** – adres/ID zależny od wersji. PCH już includuje RE (przez Starfield.h), więc RE::PlayerCamera, RE::InputEvent, RE::ButtonEvent są dostępne. W BSInputEventUser.h: `InputEvent::eventType` to enum, `ButtonEvent` ma `idCode` i `value`.

### 6. **Papyrus/SAFScript.cpp**
- **GetHandle()** – obecnie używany jest `reinterpret_cast<Papyrus::VMHandle>(a_obj)`. W Starfield prawdziwy handle obiektu skryptowego to zwykle inna wartość (np. z IObjectHandlePolicy). Na razie to placeholder; docelowo: pobrać handle przez VM / Object API (np. GetBoundHandle lub odpowiednik w CommonLibSF).
- **Bind(RE::BSScript::IVirtualMachine*)** – rejestracja natywnych funkcji; sygnatury (IVirtualMachine, std::uint32_t stackID, Object*, …) są zgodne z typami z RE.

### 7. **Commands/BetterConsole/SAF_UI.cpp**
- Używa RE::TESObjectREFR i cast do RE::Actor – poprawne przy założeniu, że referencje to aktorzy.

### 8. **Animation/Face/Manager.h**
- Zakomentowane Load/Save z RE::BGSLoadGameBuffer / BGSSaveGameBuffer – do ewentualnego dopasowania do Starfield save API (jeśli się pojawi w CommonLibSF).

---

## Podsumowanie

- Wszystkie użyte typy RE:: (Actor, TESNPC, TESRace, PlayerCharacter, TESForm, BSScript::*, NiNode, InputEvent, itd.) występują w CommonLibSF.
- Poprawione zostały: nazwa metody VFuncHook (GetOriginal), dostęp do eventType w InputEvent oraz kwalifikacja EventType w SAFScript.
- Do dopracowania przy zmianie wersji gry / CommonLibSF: relokacje w SaveLoadListener i Input, ewentualne zmiany w IStackCallbackFunctor (VM.cpp) oraz docelowe API GetHandle w SAFScript i GetSkeletonModel w SkeletonImpl.
