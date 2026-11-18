#include "ForjaCues.h"
#include "ForjaCore.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"

void UForjaCues::Executar(UWorld* Mundo, const FString& EventoJson)
{
	forja::Json E;
	if (!Mundo || !forja::Json::parse(std::string(TCHAR_TO_UTF8(*EventoJson)), E)) return;
	const std::string Tipo = E.get("tipo").as_str();
	const int64 Exec = E.get("exec").as_int();
	if (Tipo == "exec.inicio") { AparenciaPorExecucao.Add(Exec, FString(UTF8_TO_TCHAR(E.get("aparencia").dump().c_str()))); return; }
	const forja::Json& P = E.has("centro") ? E.get("centro") : (E.has("para") ? E.get("para") : E.get("pos"));
	if (!P.is_arr()) return;
	const FVector Local(P.at(0).as_num(), P.at(1).as_num(), P.at(2).as_num());
	const FName Chave(UTF8_TO_TCHAR(Tipo.c_str()));
	if (const TObjectPtr<UNiagaraSystem>* Sis = PorEvento.Find(Chave))
	{
		UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(Mundo, *Sis, Local);
		if (!C) return;
		C->SetVariableFloat(TEXT("User.Raio"), static_cast<float>(E.get("raio").as_num(100)));
		C->SetVariableFloat(TEXT("User.Preparo"), static_cast<float>(E.get("t_contato").as_int() - E.get("t").as_int()) / 1000.f);
		forja::Json Ap;
		if (const FString* A = AparenciaPorExecucao.Find(Exec)) forja::Json::parse(std::string(TCHAR_TO_UTF8(**A)), Ap);
		const forja::Json& M = Ap.get("materia");
		for (const char* K : { "visc", "elast", "tox", "temp", "elet", "fumaca", "brilho" })
			C->SetVariableFloat(FName(FString(TEXT("User.")) + UTF8_TO_TCHAR(K)), static_cast<float>(M.get(K).as_num(0)));
		FColor Cor = FColor::FromHex(UTF8_TO_TCHAR(M.get("cor").str_or("#ffb347").c_str()));
		C->SetVariableLinearColor(TEXT("User.Cor"), FLinearColor(Cor));
	}
	if (const TObjectPtr<USoundBase>* Som = Sons.Find(Chave)) UGameplayStatics::PlaySoundAtLocation(Mundo, *Som, Local);
}
