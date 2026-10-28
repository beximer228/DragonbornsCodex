#pragma once

// Solving the Nordic puzzles near the Dragonborn: claw doors (rings) and rotating pillar puzzles.
namespace Puzzles
{
	// Finds the unsolved puzzles nearby and starts turning them; returns a status line.
	std::string SolveNearby();

	// Called every 100 ms: one step of the work started by SolveNearby (turn a piece, open the door).
	void Update();

	// Bridge: the puzzles nearby and the state of every piece, as JSON.
	std::string Debug();

	// ESP: the unsolved puzzles within a_range (their keyhole, lever or button) and their label. Gathered on the game
	// thread about once a second while the ESP keeps asking (in Update), so any thread may call it.
	struct Mark
	{
		RE::ObjectRefHandle ref;
		std::string         label;
	};
	std::vector<Mark> Marks(float a_range);
}
