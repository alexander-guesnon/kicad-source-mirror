/**
 * @file rs274d.cpp
 * @brief functions to read the rs274d commands from a rs274d/rs274x file
 */

/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 1992-2018 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <fctsys.h>
#include <common.h>

#include <gerbview.h>
#include <gerbview_frame.h>
#include <trigo.h>
#include <gerber_file_image.h>
#include <X2_gerber_attributes.h>

#include <cmath>

/* Gerber: NOTES about some important commands found in RS274D and RS274X (G codes).
 * Some are now deprecated, but deprecated commands must be known by the Gerber reader
 * Gn =
 * G01 linear interpolation (linear trace)
 * G02, G20, G21 Circular interpolation, clockwise
 * G03, G30, G31 Circular interpolation, counterclockwise
 * G04 = comment. Since Sept 2014, file attributes and other X2 attributes can be found here
 *       if the line starts by G04 #@!
 * G06 parabolic interpolation
 * G07 Cubic Interpolation
 * G10 linear interpolation (scale x10)
 * G11 linear interpolation (0.1x range)
 * G12 linear interpolation (0.01x scale)
 * G36 Start polygon mode (called a region, because the "polygon" can include arcs)
 * G37 Stop polygon mode (and close it)
 * G54 Selection Tool (outdated)
 * G60 linear interpolation (scale x100)
 * G70 Select Units = Inches
 * G71 Select Units = Millimeters
 * G74 enable 90 deg mode for arcs (CW or CCW)
 * G75 enable 360 degrees for arcs (CW or CCW)
 * G90 mode absolute coordinates
 *
 * X, Y
 * X and Y are followed by + or - and m + n digits (not separated)
 * m = integer part
 * n = part after the comma
 * Classic formats: m = 2, n = 3 (size 2.3)
 * m = 3, n = 4 (size 3.4)
 * eg
 * GxxX00345Y-06123*
 *
 * Tools and D_CODES
 * Tool number (identification of shapes)
 * 10 to 999
 * D_CODES:
 * D01 ... D9 = command codes:
 *   D01 = activating light (pen down) when placement
 *   D02 = light extinction (pen up) when placement
 *   D03 = Flash
 *   D09 = VAPE Flash (I never see this command in gerber file)
 *   D51 = G54 preceded by -> Select VAPE
 *
 * D10 ... D999 = Identification Tool: tool selection
 */


/* Local Functions (are lower case since they are private to this source file)
**/


/**
 * Function fillFlashedGBRITEM
 * initializes a given GBRITEM so that it can draw a circle which is filled and
 * has no pen border.
 *
 * @param aGbrItem The GBRITEM to fill in.
 * @param aAperture the associated type of aperture
 * @param Dcode_index The DCODE value, like D14
 * @param aPos The center point of the flash
 * @param aSize The diameter of the round flash
 * @param aLayerNegative = true if the current layer is negative
 */
void fillFlashedGBRITEM(  GERBER_DRAW_ITEM* aGbrItem,
                          APERTURE_T        aAperture,
                          int               Dcode_index,
                          const wxPoint&    aPos,
                          wxSize            aSize,
                          bool              aLayerNegative )
{
    aGbrItem->m_Size  = aSize;
    aGbrItem->m_Start = aPos;
    aGbrItem->m_End   = aGbrItem->m_Start;
    aGbrItem->m_DCode = Dcode_index;
    aGbrItem->SetLayerPolarity( aLayerNegative );
    aGbrItem->m_Flashed = true;
    aGbrItem->SetNetAttributes( aGbrItem->m_GerberImageFile->m_NetAttributeDict );

    switch( aAperture )
    {
    case APT_POLYGON:           // flashed regular polygon
        aGbrItem->m_Shape = GBR_SPOT_POLY;
        break;

    case APT_CIRCLE:
        aGbrItem->m_Shape  = GBR_SPOT_CIRCLE;
        aGbrItem->m_Size.y = aGbrItem->m_Size.x;
        break;

    case APT_OVAL:
        aGbrItem->m_Shape = GBR_SPOT_OVAL;
        break;

    case APT_RECT:
        aGbrItem->m_Shape = GBR_SPOT_RECT;
        break;

    case APT_MACRO:
        aGbrItem->m_Shape = GBR_SPOT_MACRO;

        // Cache the bounding box for aperture macros
        aGbrItem->GetDcodeDescr()->GetMacro()->GetApertureMacroShape( aGbrItem, aPos );
        break;
    }
}


/**
 * Function fillLineGBRITEM
 * initializes a given GBRITEM so that it can draw a linear D code.
 *
 * @param aGbrItem The GERBER_DRAW_ITEM to fill in.
 * @param Dcode_index The DCODE value, like D14
 * @param aStart The starting point of the line
 * @param aEnd The ending point of the line
 * @param aPenSize The size of the flash. Note rectangular shapes are legal.
 * @param aLayerNegative = true if the current layer is negative
 */
void fillLineGBRITEM(  GERBER_DRAW_ITEM* aGbrItem,
                              int               Dcode_index,
                              const wxPoint&    aStart,
                              const wxPoint&    aEnd,
                              wxSize            aPenSize,
                              bool              aLayerNegative  )
{
    aGbrItem->m_Flashed = false;

    aGbrItem->m_Size = aPenSize;

    aGbrItem->m_Start = aStart;
    aGbrItem->m_End   = aEnd;

    aGbrItem->m_DCode = Dcode_index;
    aGbrItem->SetLayerPolarity( aLayerNegative );

    aGbrItem->SetNetAttributes( aGbrItem->m_GerberImageFile->m_NetAttributeDict );
}


/**
 * Function fillArcGBRITEM
 * initializes a given GBRITEM so that it can draw an arc G code.
 * <p>
 * if multiquadrant == true : arc can be 0 to 360 degrees
 *   and \a rel_center is the center coordinate relative to start point.
 * <p>
 * if multiquadrant == false arc can be only 0 to 90 deg,
 *     and only in the same quadrant :
 * <ul>
 * <li> absolute angle 0 to 90 (quadrant 1) or
 * <li> absolute angle 90 to 180 (quadrant 2) or
 * <li> absolute angle 180 to 270 (quadrant 3) or
 * <li> absolute angle 270 to 0 (quadrant 4)
 * </ul><p>
 * @param aGbrItem is the GBRITEM to fill in.
 * @param Dcode_index is the DCODE value, like D14
 * @param aStart is the starting point
 * @param aEnd is the ending point
 * @param aRelCenter is the center coordinate relative to start point,
 *   given in ABSOLUTE VALUE and the sign of values x et y de rel_center
 *   must be calculated from the previously given constraint: arc only in the same quadrant.
 * @param aClockwise true if arc must be created clockwise
 * @param aPenSize The size of the flash. Note rectangular shapes are legal.
 * @param aMultiquadrant = true to create arcs upto 360 deg,
 *                      false when arc is inside one quadrant
 * @param aLayerNegative = true if the current layer is negative
 */
void fillArcGBRITEM(  GERBER_DRAW_ITEM* aGbrItem, int Dcode_index,
                      const wxPoint& aStart, const wxPoint& aEnd,
                      const wxPoint& aRelCenter, wxSize aPenSize,
                      bool aClockwise, bool aMultiquadrant,
                      bool aLayerNegative  )
{
    wxPoint center, delta;

    aGbrItem->m_Shape = GBR_ARC;
    aGbrItem->m_Size  = aPenSize;
    aGbrItem->m_Flashed = false;

    if( aGbrItem->m_GerberImageFile )
        aGbrItem->SetNetAttributes( aGbrItem->m_GerberImageFile->m_NetAttributeDict );

    if( aMultiquadrant )
        center = aStart + aRelCenter;
    else
    {
        // in single quadrant mode the relative coordinate aRelCenter is always >= 0
        // So we must recalculate the actual sign of aRelCenter.x and aRelCenter.y
        center = aRelCenter;

        // calculate arc end coordinate relative to the starting point,
        // because center is relative to the center point
        delta  = aEnd - aStart;

        // now calculate the relative to aStart center position, for a draw function
        // that use trigonometric arc angle (or counter-clockwise)
        /* Quadrants:
         *    Y
         *  2 | 1
         * -------X
         *  3 | 4
         * C = actual relative arc center, S = arc start (axis origin) E = relative arc end
         */
        if( (delta.x >= 0) && (delta.y >= 0) )
        {
        /* Quadrant 1 (trigo or cclockwise):
         *  C | E
         * ---S---
         *  3 | 4
         */
            center.x = -center.x;
        }
        else if( (delta.x >= 0) && (delta.y < 0) )
        {
        /* Quadrant 4 (trigo or cclockwise):
         *  2 | C
         * ---S---
         *  3 | E
         */
        // Nothing to do
        }
        else if( (delta.x < 0) && (delta.y >= 0) )
        {
        /* Quadrant 2 (trigo or cclockwise):
         *  E | 1
         * ---S---
         *  C | 4
         */
            center.x = -center.x;
            center.y = -center.y;
        }
        else
        {
        /* Quadrant 3 (trigo or cclockwise):
         *  2 | 1
         * ---S---
         *  E | C
         */
            center.y = -center.y;
        }

        // Due to your draw arc function, we need this:
        if( !aClockwise )
            center = - center;

        // Calculate actual arc center coordinate:
        center += aStart;
    }

    if( aClockwise )
    {
        aGbrItem->m_Start = aStart;
        aGbrItem->m_End   = aEnd;
    }
    else
    {
        aGbrItem->m_Start = aEnd;
        aGbrItem->m_End   = aStart;
    }

    aGbrItem->m_ArcCentre = center;

    aGbrItem->m_DCode     = Dcode_index;
    aGbrItem->SetLayerPolarity( aLayerNegative );
}


/**
 * Function fillArcPOLY
 * creates an arc G code when found in poly outlines.
 * <p>
 * if multiquadrant == true : arc can be 0 to 360 degrees
 *   and \a rel_center is the center coordinate relative to start point.
 * <p>
 * if multiquadrant == false arc can be only 0 to 90 deg,
 *     and only in the same quadrant :
 * <ul>
 * <li> absolute angle 0 to 90 (quadrant 1) or
 * <li> absolute angle 90 to 180 (quadrant 2) or
 * <li> absolute angle 180 to 270 (quadrant 3) or
 * <li> absolute angle 270 to 0 (quadrant 4)
 * </ul><p>
 * @param aGbrItem is the GBRITEM to fill in.
 * @param aStart is the starting point
 * @param aEnd is the ending point
 * @param rel_center is the center coordinate relative to start point,
 *   given in ABSOLUTE VALUE and the sign of values x et y de rel_center
 *   must be calculated from the previously given constraint: arc only in the
 * same quadrant.
 * @param aClockwise true if arc must be created clockwise
 * @param aMultiquadrant = true to create arcs upto 360 deg,
 *                      false when arc is inside one quadrant
 * @param aLayerNegative = true if the current layer is negative
 */
static void fillArcPOLY(  GERBER_DRAW_ITEM* aGbrItem,
                          const wxPoint& aStart, const wxPoint& aEnd,
                          const wxPoint& rel_center,
                          bool aClockwise, bool aMultiquadrant,
                          bool aLayerNegative  )
{
    /* in order to calculate arc parameters, we use fillArcGBRITEM
     * so we muse create a dummy track and use its geometric parameters
     */
    static GERBER_DRAW_ITEM dummyGbrItem( NULL );

    aGbrItem->SetLayerPolarity( aLayerNegative );

    fillArcGBRITEM(  &dummyGbrItem, 0,
                     aStart, aEnd, rel_center, wxSize(0, 0),
                     aClockwise, aMultiquadrant, aLayerNegative );

    aGbrItem->SetNetAttributes( aGbrItem->m_GerberImageFile->m_NetAttributeDict );

    wxPoint   center;
    center = dummyGbrItem.m_ArcCentre;

    // Calculate coordinates relative to arc center;
    wxPoint start = dummyGbrItem.m_Start - center;
    wxPoint end   = dummyGbrItem.m_End - center;

    /* Calculate angle arc
     * angles are in 0.1 deg
     * angle is trigonometrical (counter-clockwise),
     * and axis is the X,Y gerber coordinates
     */
    double start_angle = ArcTangente( start.y, start.x );
    double end_angle   = ArcTangente( end.y, end.x );

    // dummyTrack has right geometric parameters, but
    // fillArcGBRITEM calculates arc parameters for a draw function that expects
    // start_angle < end_angle. So ensure this is the case here:
    // Due to the fact atan2 returns angles between -180 to + 180 degrees,
    // this is not always the case ( a modulo 360.0 degrees can be lost )
    if( start_angle > end_angle )
        end_angle += 3600;

    double arc_angle = start_angle - end_angle;
    // Approximate arc by 36 segments per 360 degree
    const int increment_angle = 3600 / 36;
    int count = std::abs( arc_angle / increment_angle );

    if( aGbrItem->m_Polygon.OutlineCount() == 0 )
        aGbrItem->m_Polygon.NewOutline();

    // calculate polygon corners
    // when arc is counter-clockwise, dummyGbrItem arc goes from end to start
    // and we must always create a polygon from start to end.
    wxPoint start_arc = start;
    for( int ii = 0; ii <= count; ii++ )
    {
        double rot;
        wxPoint end_arc = start;
        if( aClockwise )
            rot = ii * increment_angle; // rot is in 0.1 deg
        else
            rot = (count - ii) * increment_angle; // rot is in 0.1 deg

        if( ii < count )
                RotatePoint( &end_arc, -rot );
        else    // last point
            end_arc = aClockwise ? end : start;

        aGbrItem->m_Polygon.Append( VECTOR2I( end_arc + center ) );

        start_arc = end_arc;
    }
}


/* Read the Gnn sequence and returns the value nn.
 */
int GERBER_FILE_IMAGE::GCodeNumber( char*& Text )
{
    int   ii = 0;
    char* text;
    char  line[1024];

    if( Text == NULL )
        return 0;
    Text++;
    text = line;
    while( IsNumber( *Text ) )
    {
        *(text++) = *(Text++);
    }

    *text = 0;
    ii    = atoi( line );
    return ii;
}


/* Get the sequence Dnn and returns the value nn
 */
int GERBER_FILE_IMAGE::DCodeNumber( char*& Text )
{
    int   ii = 0;
    char* text;
    char  line[1024];

    if( Text == NULL )
        return 0;

    Text++;
    text = line;
    while( IsNumber( *Text ) )
        *(text++) = *(Text++);

    *text = 0;
    ii    = atoi( line );
    return ii;
}


bool GERBER_FILE_IMAGE::Execute_G_Command( char*& text, int G_command )
{
//    D( printf( "%22s: G_CODE<%d>\n", __func__, G_command ); )

    switch( G_command )
    {
    case GC_PHOTO_MODE:     // can starts a D03 flash command: redundant, can
                            // be safely ignored
        break;

    case GC_LINEAR_INTERPOL_1X:
        m_Iterpolation = GERB_INTERPOL_LINEAR_1X;
        break;

    case GC_CIRCLE_NEG_INTERPOL:
        m_Iterpolation = GERB_INTERPOL_ARC_NEG;
        break;

    case GC_CIRCLE_POS_INTERPOL:
        m_Iterpolation = GERB_INTERPOL_ARC_POS;
        break;

    case GC_COMMENT:
        // Skip comment, but only if the line does not start by "G04 #@! "
        // which is a metadata, i.e. a X2 command inside the comment.
        // this comment is called a "structured comment"
        if( strncmp( text, " #@! ", 5 ) == 0 )
        {
            text += 5;
            // The string starting at text is the same as the X2 attribute,
            // but a X2 attribute ends by '%'. So we build the X2 attribute string
            std::string x2buf;

            while( *text && (*text != '*') )
            {
                x2buf += *text;
                text++;
            }
            // add the end of X2 attribute string
            x2buf += "*%";
            x2buf += '\0';

            char* cptr = (char*)x2buf.data();
            int code_command = ReadXCommandID( cptr );
            ExecuteRS274XCommand( code_command, NULL, 0, cptr );
        }

        while( *text && (*text != '*') )
            text++;
        break;

    case GC_SELECT_TOOL:
    {
        int D_commande = DCodeNumber( text );
        if( D_commande < FIRST_DCODE )
            return false;
        if( D_commande > (TOOLS_MAX_COUNT - 1) )
            D_commande = TOOLS_MAX_COUNT - 1;
        m_Current_Tool = D_commande;
        D_CODE* pt_Dcode = GetDCODE( D_commande );
        if( pt_Dcode )
            pt_Dcode->m_InUse = true;
        break;
    }

    case GC_SPECIFY_INCHES:
        m_GerbMetric = false;           // false = Inches, true = metric
        break;

    case GC_SPECIFY_MILLIMETERS:
        m_GerbMetric = true;            // false = Inches, true = metric
        break;

    case GC_TURN_OFF_360_INTERPOL:      // disable Multi cadran arc and Arc interpol
        m_360Arc_enbl  = false;
        m_Iterpolation = GERB_INTERPOL_LINEAR_1X;   // not sure it should be done
        break;

    case GC_TURN_ON_360_INTERPOL:
        m_360Arc_enbl = true;
        break;

    case GC_SPECIFY_ABSOLUES_COORD:
        m_Relative = false;         // false = absolute Coord, true = relative
                                    // Coord
        break;

    case GC_SPECIFY_RELATIVEES_COORD:
        m_Relative = true;          // false = absolute Coord, true = relative
                                    // Coord
        break;

    case GC_TURN_ON_POLY_FILL:
        m_PolygonFillMode = true;
        m_Exposure = false;
        break;

    case GC_TURN_OFF_POLY_FILL:
        if( m_Exposure && GetItemsList() )    // End of polygon
        {
            GERBER_DRAW_ITEM * gbritem = m_Drawings.GetLast();
            gbritem->m_Polygon.Append( gbritem->m_Polygon.Vertex( 0 ) );
            StepAndRepeatItem( *gbritem );
        }
        m_Exposure = false;
        m_PolygonFillMode = false;
        m_PolygonFillModeState = 0;
        m_Iterpolation = GERB_INTERPOL_LINEAR_1X;   // not sure it should be done
        break;

    case GC_MOVE:       // Non existent
    default:
    {
        wxString msg;
        msg.Printf( wxT( "G%0.2d command not handled" ), G_command );
        AddMessageToList( msg );
        return false;
    }
    }


    return true;
}


bool GERBER_FILE_IMAGE::Execute_DCODE_Command( char*& text, int D_commande )
{
    wxSize            size( 15, 15 );

    APERTURE_T        aperture = APT_CIRCLE;
    GERBER_DRAW_ITEM* gbritem;

    int      dcode = 0;
    D_CODE*  tool  = NULL;
    wxString msg;

    if( D_commande >= FIRST_DCODE )  // This is a "Set tool" command
    {
        if( D_commande > (TOOLS_MAX_COUNT - 1) )
            D_commande = TOOLS_MAX_COUNT - 1;

        // remember which tool is selected, nothing is done with it in this
        // call
        m_Current_Tool = D_commande;

        D_CODE* pt_Dcode = GetDCODE( D_commande );
        if( pt_Dcode )
            pt_Dcode->m_InUse = true;

        return true;
    }
    else // D_commande = 0..9:  this is a pen command (usually D1, D2 or D3)
    {
        m_Last_Pen_Command = D_commande;
    }

    if( m_PolygonFillMode )    // Enter a polygon description:
    {
        switch( D_commande )
        {
        case 1:     // code D01 Draw line, exposure ON
            if( !m_Exposure )   // Start a new polygon outline:
            {
                m_Exposure = true;
                gbritem    = new GERBER_DRAW_ITEM( this );
                m_Drawings.Append( gbritem );
                gbritem->m_Shape = GBR_POLYGON;
                gbritem->m_Flashed = false;
                gbritem->m_DCode = 0;   // No DCode for a Polygon (Region in Gerber dialect)


                if( gbritem->m_GerberImageFile )
                {
                    gbritem->SetNetAttributes( gbritem->m_GerberImageFile->m_NetAttributeDict );
                    gbritem->m_AperFunction = gbritem->m_GerberImageFile->m_AperFunction;
                }
            }

            switch( m_Iterpolation )
            {
            case GERB_INTERPOL_ARC_NEG:
            case GERB_INTERPOL_ARC_POS:
                gbritem = m_Drawings.GetLast();

                fillArcPOLY( gbritem, m_PreviousPos,
                             m_CurrentPos, m_IJPos,
                             ( m_Iterpolation == GERB_INTERPOL_ARC_NEG ) ? false : true,
                             m_360Arc_enbl, GetLayerParams().m_LayerNegative );
                break;

            default:
                gbritem = m_Drawings.GetLast();

                gbritem->m_Start = m_PreviousPos;       // m_Start is used as temporary storage
                if( gbritem->m_Polygon.OutlineCount() == 0 )
                {
                    gbritem->m_Polygon.NewOutline();
                    gbritem->m_Polygon.Append( VECTOR2I( gbritem->m_Start ) );
                }

                gbritem->m_End = m_CurrentPos;       // m_End is used as temporary storage
                gbritem->m_Polygon.Append( VECTOR2I( gbritem->m_End ) );
                break;
            }

            m_PreviousPos = m_CurrentPos;
            m_PolygonFillModeState = 1;
            break;

        case 2:     // code D2: exposure OFF (i.e. "move to")
            if( m_Exposure && GetItemsList() )    // End of polygon
            {
                gbritem = m_Drawings.GetLast();
                gbritem->m_Polygon.Append( gbritem->m_Polygon.Vertex( 0 ) );
                StepAndRepeatItem( *gbritem );
            }
            m_Exposure    = false;
            m_PreviousPos = m_CurrentPos;
            m_PolygonFillModeState = 0;
            break;

        default:
            return false;
        }
    }
    else
    {
        switch( D_commande )
        {
        case 1:     // code D01 Draw line, exposure ON
            m_Exposure = true;

            tool = GetDCODE( m_Current_Tool );

            if( tool )
            {
                size     = tool->m_Size;
                dcode    = tool->m_Num_Dcode;
                aperture = tool->m_Shape;
            }

            switch( m_Iterpolation )
            {
            case GERB_INTERPOL_LINEAR_1X:
                gbritem = new GERBER_DRAW_ITEM( this );
                m_Drawings.Append( gbritem );

                fillLineGBRITEM( gbritem, dcode, m_PreviousPos,
                                 m_CurrentPos, size, GetLayerParams().m_LayerNegative );
                StepAndRepeatItem( *gbritem );
                break;

            case GERB_INTERPOL_ARC_NEG:
            case GERB_INTERPOL_ARC_POS:
                gbritem = new GERBER_DRAW_ITEM( this );
                m_Drawings.Append( gbritem );

                if( m_LastCoordIsIJPos )
                {
                    fillArcGBRITEM( gbritem, dcode, m_PreviousPos,
                                    m_CurrentPos, m_IJPos, size,
                                    ( m_Iterpolation == GERB_INTERPOL_ARC_NEG ) ?
                                    false : true, m_360Arc_enbl, GetLayerParams().m_LayerNegative );
                    m_LastCoordIsIJPos = false;
                }
                else
                {
                    fillLineGBRITEM( gbritem, dcode, m_PreviousPos,
                                     m_CurrentPos, size, GetLayerParams().m_LayerNegative );
                }

                StepAndRepeatItem( *gbritem );

                break;

            default:
                msg.Printf( wxT( "RS274D: DCODE Command: interpol error (type %X)" ),
                            m_Iterpolation );
                AddMessageToList( msg );
                break;
            }

            m_PreviousPos = m_CurrentPos;
            break;

        case 2:     // code D2: exposure OFF (i.e. "move to")
            m_Exposure = false;
            m_PreviousPos = m_CurrentPos;
            break;

        case 3:     // code D3: flash aperture
            tool = GetDCODE( m_Current_Tool );
            if( tool )
            {
                size     = tool->m_Size;
                dcode    = tool->m_Num_Dcode;
                aperture = tool->m_Shape;
            }

            gbritem = new GERBER_DRAW_ITEM( this );
            m_Drawings.Append( gbritem );
            fillFlashedGBRITEM( gbritem, aperture, dcode, m_CurrentPos,
                                size, GetLayerParams().m_LayerNegative );
            StepAndRepeatItem( *gbritem );
            m_PreviousPos = m_CurrentPos;
            break;

        default:
            return false;
        }
    }

    return true;
}
