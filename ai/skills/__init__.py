from .stem_splitter import StemSplitter
from .groove_matcher import GrooveMatcher
from .ghost_engineer import GhostEngineer
from .temporal_arranger import TemporalArranger
from .style_transfer import StyleTransfer

SKILLS = {
    "stem_splitter": StemSplitter,
    "groove_matcher": GrooveMatcher,
    "ghost_engineer": GhostEngineer,
    "temporal_arranger": TemporalArranger,
    "style_transfer": StyleTransfer,
}
