// Tabela de cues: tipo de evento do runtime -> sistema Niagara + som. A matéria da criação (viscosidade,
// elasticidade, toxicidade, temperatura, eletricidade, fumaça, cores) vira parâmetros de usuário Niagara.
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ForjaCues.generated.h"

class UNiagaraSystem;
class USoundBase;

UCLASS(BlueprintType)
class FORJAARENA_API UForjaCues : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	/** Chaves: area.anuncio, area.contato, area.fim, area.cancelada, guarda.inicio, guarda.absorve, guarda.fim, mov.inicio, mov.fim, impacto, marca. */
	UPROPERTY(EditAnywhere, Category = "Forja") TMap<FName, TObjectPtr<UNiagaraSystem>> PorEvento;
	UPROPERTY(EditAnywhere, Category = "Forja") TMap<FName, TObjectPtr<USoundBase>> Sons;
	void Executar(UWorld* Mundo, const FString& EventoJson);
private:
	TMap<int64, FString> AparenciaPorExecucao;
};
