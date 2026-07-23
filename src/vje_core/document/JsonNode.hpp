//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2026
// Author:  Rohin Gosling
//
// Description:
//
//   JsonNode -- one node of the in-memory document tree. The model satisfies the two fidelity constraints Qt's
//   QJsonDocument cannot: object members are held in an INSERTION-ORDERED container (member order preserved,
//   FILE-04) and numbers hold their exact source lexeme (the raw token, never a binary conversion, FILE-10).
//
//   Objects tolerate duplicate sibling keys (a file may be loaded with them, RFC 8259 permits them); order is
//   preserved by storing keys and child values in parallel, insertion-ordered vectors. Uniqueness is enforced by
//   the edit paths (VAL-02), not by this container.
//
//   Ownership: a node owns its children by std::unique_ptr; each child keeps a non-owning back-pointer to its
//   parent. Nodes are non-copyable (deep copies go through clone()) and are not QObjects -- change notification
//   lives on JsonDocument, keeping the node lightweight.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace vje
{
	//-----------------------------------------------------------------------------------------------------------------
	// The six JSON value kinds. PascalCase per the enum-class convention (and to avoid the NULL/TRUE/FALSE macros).
	//-----------------------------------------------------------------------------------------------------------------

	enum class JsonKind
	{
		Null,
		Boolean,
		Number,
		String,
		Array,
		Object
	};

	//*****************************************************************************************************************
	// Class: JsonNode
	//*****************************************************************************************************************

	class JsonNode
	{
		//=============================================================================================================
		// Data Members
		//=============================================================================================================

	private:

		JsonKind  nodeKind;                                    // Which of the six kinds this node is.
		JsonNode* parentNode;                                  // Non-owning back-pointer; nullptr for a root.
		QString   scalarText;                                  // String value, or the raw number token (by kind).
		bool      booleanValue;                                // Boolean value (kind == Boolean).

		std::vector<std::unique_ptr<JsonNode>> children;       // Array elements, or object member values.
		QStringList                            memberKeys;     // Object member keys, parallel to children (Object only).

		//=============================================================================================================
		// Constructors / Destructor
		//=============================================================================================================

	public:

		explicit JsonNode ( JsonKind kind );

		JsonNode ( const JsonNode& )            = delete;      // Non-copyable; use clone().
		JsonNode& operator= ( const JsonNode& ) = delete;

		//=============================================================================================================
		// Factories -- the idiomatic way to build a node of each kind.
		//=============================================================================================================

	public:

		static std::unique_ptr<JsonNode> make_null    ();
		static std::unique_ptr<JsonNode> make_boolean ( bool value );
		static std::unique_ptr<JsonNode> make_number  ( const QString& rawToken );
		static std::unique_ptr<JsonNode> make_string  ( const QString& value );
		static std::unique_ptr<JsonNode> make_array   ();
		static std::unique_ptr<JsonNode> make_object  ();

		//=============================================================================================================
		// Value Accessors
		//=============================================================================================================

	public:

		JsonKind  kind   () const;
		JsonNode* parent () const;
		int       index_in_parent () const;                    // This node's position among its parent's children; -1 if root.

		bool           is_scalar    () const;                  // Null / Boolean / Number / String.
		bool           is_container () const;                  // Array / Object.

		bool           boolean_value () const;                 // Precondition: kind == Boolean.
		const QString& number_token  () const;                 // Raw lexeme; precondition: kind == Number.
		const QString& string_value  () const;                 // Precondition: kind == String.

		// Array access (precondition: kind == Array).

		int       array_size    () const;
		JsonNode* array_element ( int index ) const;           // nullptr if out of range.

		// Object access (precondition: kind == Object).

		int            member_count ( ) const;
		const QString& member_key   ( int index ) const;
		JsonNode*      member_value ( int index ) const;       // nullptr if out of range.
		JsonNode*      find_member  ( const QString& key ) const;   // First match; nullptr if absent.
		bool           has_member   ( const QString& key ) const;
		int            key_count    ( const QString& key ) const;   // Occurrences (>= 2 means duplicate).

		//=============================================================================================================
		// Mutators
		//=============================================================================================================

	public:

		void set_boolean_value ( bool value );                 // Precondition: kind == Boolean.
		void set_number_token  ( const QString& rawToken );    // Precondition: kind == Number.
		void set_string_value  ( const QString& value );       // Precondition: kind == String.

		JsonNode* append_element ( std::unique_ptr<JsonNode> element );                 // Array; returns the child.
		JsonNode* append_member  ( const QString& key, std::unique_ptr<JsonNode> value ); // Object; returns the child.

		// Structural mutators used by the edit commands (editing/). All take/return ownership by unique_ptr and keep
		// the parent back-pointer consistent; indices are validated by the caller (the command layer). Array variants
		// require kind == Array; member variants require kind == Object.

		void                      insert_element  ( int index, std::unique_ptr<JsonNode> element );  // index in [0, size].
		std::unique_ptr<JsonNode> take_element    ( int index );                       // Detach + return; null if OOR.
		std::unique_ptr<JsonNode> replace_element ( int index, std::unique_ptr<JsonNode> element );  // Returns the old.

		void                      insert_member       ( int index, const QString& key, std::unique_ptr<JsonNode> value );
		std::unique_ptr<JsonNode> take_member         ( int index, QString* outKey = nullptr );  // Detach; optional key out.
		std::unique_ptr<JsonNode> replace_member_value ( int index, std::unique_ptr<JsonNode> value );  // Key unchanged.
		void                      set_member_key      ( int index, const QString& key );         // Rename (EDIT-02).

		void                      move_child ( int fromIndex, int toIndex );           // Reorder within this container.

		//=============================================================================================================
		// Methods
		//=============================================================================================================

	public:

		std::unique_ptr<JsonNode> clone () const;              // Deep copy.

		// Deep structural equality: same kind, and -- order-sensitively -- same scalar value / raw number token /
		// member keys / element and member values. Member order is significant (FILE-04).

		bool equals ( const JsonNode& other ) const;
	};
}
