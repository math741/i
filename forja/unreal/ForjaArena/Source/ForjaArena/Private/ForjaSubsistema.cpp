#include "ForjaSubsistema.h"
#include "ForjaCore.h"
#include "ForjaEstadoJogo.h"
#include "ForjaPersonagem.h"
#include "ForjaAtributos.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace
{
	std::string Utf8(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
	FString DeUtf8(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
	forja::Vec3 PesDe(const ACharacter* C)
	{
		const FVector L = C->GetActorLocation() - FVector(0, 0, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		return forja::Vec3(L.X, L.Y, L.Z);  // mesmos eixos e unidades (cm, X frente, Y direita, Z cima)
	}
}

bool UForjaSubsistema::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* W = Cast<UWorld>(Outer);
	return W && W->IsGameWorld() && W->GetNetMode() != NM_Client;  // só onde há autoridade
}

void UForjaSubsistema::Initialize(FSubsystemCollectionBase& Colecao)
{
	Super::Initialize(Colecao);
	T0 = FPlatformTime::Seconds();
	Partida = new forja::Match(forja::MatchConfig::for_mode("duelo"));
}

void UForjaSubsistema::Deinitialize()
{
	delete Partida;
	Partida = nullptr;
	Super::Deinitialize();
}

int64 UForjaSubsistema::AgoraMs() const { return static_cast<int64>((FPlatformTime::Seconds() - T0) * 1000.0); }

int32 UForjaSubsistema::RegistrarJogador(AForjaPersonagem* P, const FString& Nome, const FString& Token)
{
	const int32 Assento = Partida->join(Utf8(Nome), Utf8(Token), AgoraMs());
	if (Assento >= 0) PorAssento.Add(Assento, P);
	return Assento;
}

void UForjaSubsistema::Desconectar(AForjaPersonagem* P)
{
	for (auto& Par : PorAssento) if (Par.Value.Get() == P) { Partida->disconnect(Par.Key, AgoraMs()); }
}

FString UForjaSubsistema::Publicar(const FString& PacoteJson)
{
	forja::Json Pacote;
	std::string Erro;
	if (!forja::Json::parse(Utf8(PacoteJson), Pacote, &Erro)) return DeUtf8("{\"ok\":false,\"erro\":\"JSON inválido\"}");
	return DeUtf8(Partida->publish(Pacote).dump());
}

FString UForjaSubsistema::Equipar(int32 Assento, int32 Espaco, const FString& Digest)
{
	return DeUtf8(Partida->equip(Assento, Espaco, Utf8(Digest), AgoraMs()).dump());
}

FString UForjaSubsistema::Botao(int32 Assento, int32 Espaco, const FString& Tipo, int64 TCliente, float MiraYaw, const FVector& Mira)
{
	const int64 Agora = AgoraMs();
	Tick(0.f);  // mundo sincronizado até agora antes de julgar
	const forja::Runtime::Act A = Partida->press(Assento, Espaco, Tipo == TEXT("soltar"), Tipo == TEXT("secundario"), TCliente, Agora, MiraYaw, forja::Vec3(Mira.X, Mira.Y, Mira.Z));
	forja::Json R = forja::Json::obj();
	R.set("ok", A.ok); R.set("exec", A.exec); R.set("motivo", A.motivo); R.set("foi_comando", A.foi_comando); R.set("t_julgado", A.t_julgado);
	return DeUtf8(R.dump());
}

FString UForjaSubsistema::ComandoSala(int32 Assento, const FString& ComandoJson)
{
	forja::Json C;
	if (!forja::Json::parse(Utf8(ComandoJson), C)) return TEXT("{\"ok\":false}");
	const std::string K = C.get("cmd").as_str();
	const int64 Agora = AgoraMs();
	forja::Json R;
	if (K == "sala.treino") R = Partida->start_training(Agora);
	else if (K == "sala.duelo") R = Partida->start_duel(Agora);
	else if (K == "sala.revanche") R = Partida->request_rematch(Assento, Agora);
	else if (K == "sala.pronto") { Partida->set_ready(Assento, C.get("valor").as_bool(true), Agora); R = forja::Json::obj(); R.set("ok", true); }
	else R = Partida->training(C, Assento, Agora);
	return DeUtf8(R.dump());
}

void UForjaSubsistema::Tick(float DeltaTime)
{
	if (!Partida) return;
	const int64 Agora = AgoraMs();
	forja::Runtime& RT = Partida->rt();
	// 1) posições: fora de deslocamento manda o CharacterMovement; durante um deslocamento da receita manda o runtime
	for (auto& Par : PorAssento)
	{
		AForjaPersonagem* P = Par.Value.Get();
		const forja::Seat* S = Partida->seat(Par.Key);
		if (!P || !S || S->ent < 0) continue;
		forja::Entity* E = RT.ent(S->ent);
		if (!E) continue;
		if (E->em_mov)
		{
			const float Meia = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			P->SetActorLocation(FVector(E->pos.x, E->pos.y, E->pos.z + Meia), false, nullptr, ETeleportType::TeleportPhysics);
		}
		else RT.sync_external(S->ent, PesDe(P), P->GetActorRotation().Yaw, Agora);
		if (P->Atributos) P->Atributos->SetFolego(static_cast<float>(E->folego));
	}
	// 2) regras da partida e eventos para os clientes
	Partida->tick(Agora);
	const std::vector<forja::Json> Eventos = Partida->drain();
	if (Eventos.empty()) return;
	TArray<FString> Lista;
	Lista.Reserve(Eventos.size());
	for (const forja::Json& E : Eventos)
	{
		forja::Json X = E;
		if (X.get("tipo").as_str() == "exec.inicio") X.set("aparencia", Partida->aparencia_of(X.get("digest").as_str()));
		Lista.Add(DeUtf8(X.dump()));
	}
	if (AForjaEstadoJogo* GS = GetWorld()->GetGameState<AForjaEstadoJogo>())
	{
		GS->Multicast_Eventos(Lista);
		if (Partida->fase() != Utf8(UltimaFase)) { UltimaFase = DeUtf8(Partida->fase()); GS->EstadoSala = DeUtf8(Partida->room_state().dump()); }
	}
}
