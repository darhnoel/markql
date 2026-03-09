namespace MarkQLCore

def acceptSelectDivFromDoc : String := "SELECT div FROM doc"
def acceptSelectStarFromDoc : String := "SELECT * FROM doc"
def acceptSelectDivFromDocAsN : String := "SELECT div FROM doc AS n"
def acceptSelectTitleFromDoc : String := "SELECT title FROM doc"
def acceptSelectStarFromDocAsNode : String := "SELECT * FROM doc AS node_row"

def rejectSelectFromDoc : String := "SELECT FROM doc"
def rejectSelectDivDoc : String := "SELECT div doc"
def rejectFromDocSelectDiv : String := "FROM doc SELECT div"
def rejectSelectDivFrom : String := "SELECT div FROM"
def rejectSelectDivAsN : String := "SELECT div AS n"
def rejectSelectDivFromDocAsSelf : String := "SELECT div FROM doc AS self"

def acceptedExamples : List String :=
  [ acceptSelectDivFromDoc
  , acceptSelectStarFromDoc
  , acceptSelectDivFromDocAsN
  , acceptSelectTitleFromDoc
  , acceptSelectStarFromDocAsNode
  ]

def rejectedExamples : List String :=
  [ rejectSelectFromDoc
  , rejectSelectDivDoc
  , rejectFromDocSelectDiv
  , rejectSelectDivFrom
  , rejectSelectDivAsN
  , rejectSelectDivFromDocAsSelf
  ]

end MarkQLCore
