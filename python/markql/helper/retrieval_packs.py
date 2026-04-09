from __future__ import annotations

from .. import _core
from . import local_core
from .schemas import RetrievalPack, RetrievalTopic


def get_retrieval_pack(topic: RetrievalTopic) -> RetrievalPack:
    if _core is not None and hasattr(_core, "helper_retrieval_pack"):
        return dict(_core.helper_retrieval_pack(topic))  # type: ignore[return-value]
    return local_core.helper_retrieval_pack(topic)
