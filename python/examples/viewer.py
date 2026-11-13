"""Run with an installed Minvex wheel: python viewer.py model.glb --clip Walk."""
import argparse
import minvex as mvx


def main():
    parser = argparse.ArgumentParser(description="Minvex Python Viewer")
    parser.add_argument("model")
    parser.add_argument("--clip", help="Animation name (or use the model's first clip)")
    parser.add_argument("--frames", type=int, default=0)
    args = parser.parse_args()
    with mvx.Viewer() as viewer:
        model = viewer.add_model(args.model)
        if model.animation_clips:
            model.play_animation(args.clip or 0)
        for node in model.morph_nodes:
            weights = node["weights"]
            weights[0] = .5
            model.set_morph_weights(node["node"], weights)
        viewer.run(frame_limit=args.frames, on_event=lambda event: print(
            event.object, event.occurrence.event.name, event.occurrence.event.payload))


if __name__ == "__main__":
    main()
